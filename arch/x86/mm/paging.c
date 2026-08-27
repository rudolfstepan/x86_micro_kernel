/**
 * @file arch/x86/mm/paging.c
 * @brief Physische Frames, Seitentabellen und Adressraumisolation.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Userkopien validieren jede Seite vor Zugriff; Kernelmappings bleiben supervisor-only.
 */
#include "arch/x86/mm/paging.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/interrupt.h"
#include "kernel/time/apic.h"
#include "include/kernel/panic.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "mm/kmalloc.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

_Static_assert(sizeof(page_table_entry_t) == sizeof(uint32_t),
               "x86 page-table entries must be 32 bits");
_Static_assert(sizeof(page_directory_entry_t) == sizeof(uint32_t),
               "x86 page-directory entries must be 32 bits");
_Static_assert(KERNEL_IDENTITY_LIMIT == USER_BASE,
               "kernel direct map must end at the user-space boundary");

uint32_t page_directory[PAGE_DIRECTORY_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t kernel_page_tables[KERNEL_PAGE_ENTRIES][PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

/* Every supervisor address space shares these high page tables.  Keeping the
 * PDEs stable before the first Ring-3 process exists lets later MMIO PTEs
 * become visible in all current and future CR3s without exposing PAGE_USER. */
#define KERNEL_HIGH_PAGE_ENTRIES (PAGE_DIRECTORY_ENTRIES - USER_PAGE_END)
static uint32_t
    kernel_high_page_tables[KERNEL_HIGH_PAGE_ENTRIES][PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

#define LOCAL_APIC_BASE 0xFEE00000U
#define LOCAL_APIC_DIRECTORY_INDEX (LOCAL_APIC_BASE >> 22)
#define LOCAL_APIC_TABLE_INDEX ((LOCAL_APIC_BASE >> 12) & 0x3FFU)
static bool paging_enabled;
static bool pat_checked;
static bool pat_write_combining;
static spinlock_t page_table_lock = SPINLOCK_INIT;

/* A CPU waiting for the page-table lock must remain able to acknowledge a
 * shootdown initiated by the current owner.  Keeping IF clear for the entire
 * spin creates a cross-CPU deadlock: the owner waits for the TLB ACK while the
 * target waits for this lock and cannot enter the IPI handler. */
static uint32_t page_table_lock_acquire_irq(void) {
    uint32_t flags = irq_save();
    for (uint32_t spin = 0U; spin < SPINLOCK_ACQUIRE_SPIN_LIMIT; ++spin) {
        if (spinlock_trylock(&page_table_lock)) return flags;
        if ((flags & 0x200U) != 0U) {
            __asm__ __volatile__("sti\n\tpause\n\tcli" : : : "memory");
        } else {
            __asm__ __volatile__("pause");
        }
    }
    uint32_t cpu = x86_cpu_current_index();
    panic_context_set("memory", "page-table lock", "acquire",
                      "IPI-responsive bounded wait");
    panic_context_set_result(
        -110, (uint32_t)(uintptr_t)&page_table_lock,
        (cpu << 16U) | (page_table_lock.owner_cpu & 0xFFFFU));
    panic("SMP page-table lock acquisition timed out");
}

static void page_table_lock_release_irq(uint32_t flags) {
    spinlock_release_irq(&page_table_lock, flags);
}

#define TLB_SHOOTDOWN_IPI_SPIN_LIMIT (1U << 20U)
#define TLB_SHOOTDOWN_ACK_SPIN_LIMIT (1U << 24U)

typedef struct {
    volatile uint32_t generation;
    volatile uint32_t target_mask;
    volatile uint32_t acknowledged_mask;
    page_directory_t *directory;
    uint32_t virtual_address;
    bool full_flush;
    bool kernel_shared;
} tlb_shootdown_request_t;

static tlb_shootdown_request_t tlb_shootdown_request;

#define IA32_PAT_MSR 0x277U
#define CR0_CACHE_DISABLE (1U << 30U)
#define CR0_NOT_WRITE_THROUGH (1U << 29U)
#define CPUID_FEATURE_MSR (1U << 5U)
#define CPUID_FEATURE_PAT (1U << 16U)
#define CPUID_FEATURE_SSE (1U << 25U)
#define PAT_ENTRY_WIDTH 8U
#define PAT_WRITE_COMBINING 1U

extern uint8_t _stack_guard_start;

static inline void load_cr3(uint32_t address) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(address) : "memory");
}

static inline uint32_t read_cr0(void) {
    uint32_t cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

static inline void write_cr0(uint32_t cr0) {
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

static inline void flush_tlb(void) {
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    load_cr3(cr3);
}

static inline void invalidate_tlb_page(uint32_t virtual_address) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static bool tlb_shootdown_locked(page_directory_t *directory,
                                 uint32_t virtual_address,
                                 bool full_flush) {
    KASSERT_IRQ_DISABLED();
    KASSERT(spinlock_is_owned_by_current(&page_table_lock));
    KASSERT(directory != NULL);
    x86_cpu_local_t *current = x86_cpu_local_current();
    KASSERT(current != NULL && current->cpu_index < X86_CPU_LOCAL_MAX);
    bool kernel_shared = directory == paging_kernel_directory();

    if (kernel_shared || current->current_page_directory == directory) {
        if (full_flush) flush_tlb();
        else invalidate_tlb_page(virtual_address);
    }

    uint32_t target_mask = 0U;
    for (uint32_t cpu = 0U; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        x86_cpu_local_t *local = x86_cpu_local_by_index(cpu);
        if (local == NULL || local->online == 0U ||
            cpu == current->cpu_index) continue;
        if (kernel_shared || local->current_page_directory == directory)
            target_mask |= 1U << cpu;
    }
    if (target_mask == 0U) return true;
    if (!apic_is_available()) return false;

    uint32_t generation = tlb_shootdown_request.generation + 1U;
    if (generation == 0U) generation = 1U;
    tlb_shootdown_request.directory = directory;
    tlb_shootdown_request.virtual_address = virtual_address;
    tlb_shootdown_request.full_flush = full_flush;
    tlb_shootdown_request.kernel_shared = kernel_shared;
    tlb_shootdown_request.target_mask = target_mask;
    tlb_shootdown_request.acknowledged_mask = 0U;
    __sync_synchronize();
    tlb_shootdown_request.generation = generation;
    __sync_synchronize();

    for (uint32_t cpu = 0U; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        if ((target_mask & (1U << cpu)) == 0U) continue;
        x86_cpu_local_t *local = x86_cpu_local_by_index(cpu);
        if (local == NULL ||
            !apic_send_ipi_bounded((uint8_t)local->apic_id,
                APIC_IPI_FIXED(X86_TLB_SHOOTDOWN_VECTOR),
                TLB_SHOOTDOWN_IPI_SPIN_LIMIT)) return false;
    }
    for (uint32_t spin = 0U; spin < TLB_SHOOTDOWN_ACK_SPIN_LIMIT; ++spin) {
        if (tlb_shootdown_request.acknowledged_mask == target_mask)
            return true;
        __asm__ __volatile__("pause");
    }
    return false;
}

static void tlb_shootdown_or_panic(page_directory_t *directory,
                                   uint32_t virtual_address,
                                   bool full_flush) {
    if (tlb_shootdown_locked(directory, virtual_address, full_flush)) return;
    panic_context_set("memory", "TLB shootdown", "invalidate",
                      "generation acknowledgement");
    panic_context_set_result(-110, tlb_shootdown_request.target_mask,
                             tlb_shootdown_request.acknowledged_mask);
    panic("SMP TLB shootdown timed out");
}

void paging_tlb_shootdown_isr(void *frame) {
    (void)frame;
    irq_context_note_vector(X86_TLB_SHOOTDOWN_VECTOR);
    irq_context_enter();
    x86_cpu_local_t *local = x86_cpu_local_current();
    uint32_t generation = tlb_shootdown_request.generation;
    if (local != NULL && local->cpu_index < X86_CPU_LOCAL_MAX &&
        generation != 0U) {
        uint32_t bit = 1U << local->cpu_index;
        if ((tlb_shootdown_request.target_mask & bit) != 0U &&
            local->tlb_observed_generation != generation) {
            __sync_synchronize();
            if (tlb_shootdown_request.kernel_shared ||
                local->current_page_directory ==
                    tlb_shootdown_request.directory) {
                if (tlb_shootdown_request.full_flush) flush_tlb();
                else invalidate_tlb_page(
                    tlb_shootdown_request.virtual_address);
            }
            local->tlb_observed_generation = generation;
            __sync_fetch_and_or(&tlb_shootdown_request.acknowledged_mask,
                                bit);
        }
    }
    apic_eoi();
    irq_context_exit();
}

bool paging_tlb_shootdown_probe(void) {
    uint32_t flags = page_table_lock_acquire_irq();
    bool complete = tlb_shootdown_locked(paging_kernel_directory(),
                                         (uint32_t)(uintptr_t)page_directory,
                                         false);
    page_table_lock_release_irq(flags);
    return complete;
}

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32U) | low;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                         "d"((uint32_t)(value >> 32U)) : "memory");
}

static bool prepare_pat_write_combining(void) {
    if (pat_checked) return pat_write_combining;
    pat_checked = true;

    uint32_t maximum_leaf;
    uint32_t unused_b;
    uint32_t unused_c;
    uint32_t unused_d;
    __asm__ __volatile__("cpuid"
                         : "=a"(maximum_leaf), "=b"(unused_b),
                           "=c"(unused_c), "=d"(unused_d)
                         : "a"(0U));
    if (maximum_leaf < 1U) return false;

    uint32_t features;
    __asm__ __volatile__("cpuid"
                         : "=a"(unused_b), "=b"(unused_c),
                           "=c"(unused_d), "=d"(features)
                         : "a"(1U));
    uint32_t required = CPUID_FEATURE_MSR | CPUID_FEATURE_PAT |
                        CPUID_FEATURE_SSE;
    if ((features & required) != required) return false;

    uint64_t original = read_msr(IA32_PAT_MSR);
    uint64_t replacement =
        (original & ~((uint64_t)0xFFU << PAT_ENTRY_WIDTH)) |
        ((uint64_t)PAT_WRITE_COMBINING << PAT_ENTRY_WIDTH);
    if (replacement != original) {
        uint32_t flags;
        __asm__ __volatile__("pushf\n pop %0\n cli"
                             : "=r"(flags) : : "memory");
        uint32_t original_cr0 = read_cr0();
        write_cr0((original_cr0 | CR0_CACHE_DISABLE) &
                  ~CR0_NOT_WRITE_THROUGH);
        __asm__ __volatile__("wbinvd" : : : "memory");
        flush_tlb();
        write_msr(IA32_PAT_MSR, replacement);
        __asm__ __volatile__("wbinvd" : : : "memory");
        write_cr0(original_cr0);
        flush_tlb();
        __asm__ __volatile__("push %0\n popf"
                             : : "r"(flags) : "memory");
    }
    pat_write_combining =
        ((read_msr(IA32_PAT_MSR) >> PAT_ENTRY_WIDTH) & 0xFFU) ==
        PAT_WRITE_COMBINING;
    if (pat_write_combining)
        printf("PAGING: PAT write-combining enabled\n");
    return pat_write_combining;
}

static void *allocate_page(void) {
    size_t address = allocate_frame();
    if (address == 0) {
        return NULL;
    }
    memset((void*)address, 0, PAGE_SIZE);
    return (void*)address;
}

static void free_page(void *page) {
    uintptr_t address = (uintptr_t)page;
    if ((address & (PAGE_SIZE - 1U)) == 0) {
        free_frame(address);
    }
}

page_directory_t *create_page_directory(void) {
    KASSERT_NOT_IRQ();
    page_directory_t *pd = (page_directory_t*)allocate_page();
    if (pd == NULL) {
        printf("Failed to allocate page directory.\n");
        return NULL;
    }

    uint32_t flags = page_table_lock_acquire_irq();
    uint32_t *entries = (uint32_t*)pd->entries;
    /* Every address space shares supervisor-only kernel mappings.  This also
     * includes device MMIO above USER_TOP (PCI BARs, LAPIC, and similar), so
     * hardware IRQ handlers remain usable while a Ring-3 CR3 is active. */
    for (size_t i = 0; i < PAGE_DIRECTORY_ENTRIES; ++i) {
        if (i < USER_PAGE_START || i >= USER_PAGE_END) {
            entries[i] = page_directory[i];
        }
    }
    page_table_lock_release_irq(flags);
    return pd;
}

page_directory_t *paging_kernel_directory(void) {
    return (page_directory_t*)(void*)page_directory;
}

page_directory_t *paging_current_directory(void) {
    x86_cpu_local_t *local = x86_cpu_local_current();
    return local != NULL && local->current_page_directory != NULL
        ? local->current_page_directory : paging_kernel_directory();
}

void switch_page_directory(page_directory_t *pd) {
    if (pd == NULL) pd = paging_kernel_directory();
    x86_cpu_local_t *local = x86_cpu_local_current();
    if (local == NULL) return;
    local->current_page_directory = pd;
    load_cr3((uint32_t)(uintptr_t)pd);
}

void init_paging(void) {
    memset(page_directory, 0, sizeof(page_directory));
    memset(kernel_page_tables, 0, sizeof(kernel_page_tables));
    memset(kernel_high_page_tables, 0, sizeof(kernel_high_page_tables));

    for (uint32_t directory_index = 0;
         directory_index < KERNEL_PAGE_ENTRIES; ++directory_index) {
        for (uint32_t table_index = 0;
             table_index < PAGE_TABLE_ENTRIES; ++table_index) {
            uint32_t physical_address =
                (directory_index * PAGE_TABLE_ENTRIES + table_index) * PAGE_SIZE;
            if (physical_address != 0) {
                kernel_page_tables[directory_index][table_index] =
                    physical_address | PAGE_PRESENT | PAGE_RW;
            }
        }

        page_directory[directory_index] =
            (uint32_t)(uintptr_t)kernel_page_tables[directory_index] |
            PAGE_PRESENT | PAGE_RW;
    }

    for (uint32_t directory_index = USER_PAGE_END;
         directory_index < PAGE_DIRECTORY_ENTRIES; ++directory_index) {
        page_directory[directory_index] =
            (uint32_t)(uintptr_t)kernel_high_page_tables[
                directory_index - USER_PAGE_END] | PAGE_PRESENT | PAGE_RW;
    }

    /* LAPIC MMIO sits near 4 GB and needs an uncached supervisor mapping. */
    kernel_high_page_tables[
        LOCAL_APIC_DIRECTORY_INDEX - USER_PAGE_END][LOCAL_APIC_TABLE_INDEX] =
        LOCAL_APIC_BASE | PAGE_PRESENT | PAGE_RW | PAGE_CACHE_DISABLE;

    /* Boot-stack and task-stack guards must fault before any stack data can
     * corrupt an adjacent object.  Task stack pages are mapped on demand. */
    uint32_t boot_guard = (uint32_t)(uintptr_t)&_stack_guard_start;
    kernel_page_tables[boot_guard >> 22]
                      [(boot_guard >> 12) & 0x3FFU] = 0;
    for (uint32_t address = KERNEL_STACK_ARENA_BASE;
         address < KERNEL_STACK_ARENA_BASE + KERNEL_STACK_ARENA_SIZE;
         address += PAGE_SIZE) {
        kernel_page_tables[address >> 22][(address >> 12) & 0x3FFU] = 0;
    }

    switch_page_directory(paging_kernel_directory());
    write_cr0(read_cr0() | CR0_PG);
    paging_enabled = true;
    printf("Paging enabled; identity-mapped the first %u MB.\n",
           (unsigned int)(KERNEL_IDENTITY_LIMIT / 1024U / 1024U));
}

bool paging_is_enabled(void) {
    return paging_enabled;
}

bool paging_kernel_page_present(uint32_t virtual_address) {
    uint32_t flags = page_table_lock_acquire_irq();
    uint32_t pde = page_directory[virtual_address >> 22];
    if ((pde & PAGE_PRESENT) == 0) {
        page_table_lock_release_irq(flags);
        return false;
    }
    const uint32_t *table =
        (const uint32_t*)(uintptr_t)(pde & 0xFFFFF000U);
    bool present =
        (table[(virtual_address >> 12) & 0x3FFU] & PAGE_PRESENT) != 0;
    page_table_lock_release_irq(flags);
    return present;
}

int unmap_kernel_page(uint32_t virtual_address, bool free_physical_frame) {
    if ((virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        virtual_address >= USER_BASE) return -1;
    uint32_t flags = page_table_lock_acquire_irq();
    uint32_t pde = page_directory[virtual_address >> 22];
    if ((pde & PAGE_PRESENT) == 0) {
        page_table_lock_release_irq(flags);
        return -1;
    }
    uint32_t *table = (uint32_t*)(uintptr_t)(pde & 0xFFFFF000U);
    uint32_t *entry = &table[(virtual_address >> 12) & 0x3FFU];
    if ((*entry & PAGE_PRESENT) == 0) {
        page_table_lock_release_irq(flags);
        return -1;
    }
    uintptr_t physical = (uintptr_t)(*entry & 0xFFFFF000U);
    *entry = 0;
    tlb_shootdown_or_panic(paging_kernel_directory(), virtual_address, false);
    if (free_physical_frame) free_page((void*)physical);
    page_table_lock_release_irq(flags);
    return 0;
}

void test_paging(void) {
    volatile uint32_t *mapped_address = &page_directory[0];
    uint32_t value = *mapped_address;
    *mapped_address = value;
    printf("Paging identity-map test passed at %p.\n", (void*)mapped_address);
}

void free_page_directory(page_directory_t *pd) {
    if (pd == NULL || (void*)pd == (void*)page_directory) {
        return;
    }

    uint32_t flags = page_table_lock_acquire_irq();
    uint32_t *entries = (uint32_t*)pd->entries;
    /* Only the user window is private. Kernel and MMIO page tables are shared
     * with the kernel directory and must never be released with a process. */
    for (size_t i = USER_PAGE_START; i < USER_PAGE_END; ++i) {
        if ((entries[i] & PAGE_PRESENT) != 0) {
            uint32_t *table =
                (uint32_t*)(uintptr_t)(entries[i] & 0xFFFFF000U);
            for (size_t j = 0; j < PAGE_TABLE_ENTRIES; ++j) {
                if ((table[j] & (PAGE_PRESENT | PAGE_USER)) ==
                    (PAGE_PRESENT | PAGE_USER)) {
                    free_page((void*)(uintptr_t)(table[j] & 0xFFFFF000U));
                }
            }
            free_page(table);
        }
    }
    free_page(pd);
    page_table_lock_release_irq(flags);
}

int unmap_page(page_directory_t *pd, uint32_t virtual_address,
               bool free_physical_frame) {
    if (pd == NULL || virtual_address < USER_BASE ||
        virtual_address >= USER_TOP ||
        (virtual_address & (PAGE_SIZE - 1U)) != 0) return -1;
    uint32_t flags = page_table_lock_acquire_irq();
    uint32_t *directory = (uint32_t*)pd->entries;
    uint32_t di = virtual_address >> 22;
    uint32_t ti = (virtual_address >> 12) & 0x3FFU;
    if ((directory[di] & PAGE_PRESENT) == 0) {
        page_table_lock_release_irq(flags);
        return -1;
    }
    uint32_t *table = (uint32_t*)(uintptr_t)(directory[di] & 0xFFFFF000U);
    if ((table[ti] & PAGE_PRESENT) == 0) {
        page_table_lock_release_irq(flags);
        return -1;
    }
    uintptr_t physical = (uintptr_t)(table[ti] & 0xFFFFF000U);
    table[ti] = 0;
    tlb_shootdown_or_panic(pd, virtual_address, false);
    if (free_physical_frame) free_page((void*)physical);
    page_table_lock_release_irq(flags);
    return 0;
}

bool user_range_accessible(const page_directory_t *pd, uint32_t address,
                           size_t length, bool write_access) {
    if (pd == NULL || length == 0 || address < USER_BASE ||
        address >= USER_TOP || length > USER_TOP - address) return false;
    uint32_t first = address & ~(PAGE_SIZE - 1U);
    uint32_t last = (uint32_t)(address + length - 1U) & ~(PAGE_SIZE - 1U);
    const uint32_t *directory = (const uint32_t*)pd->entries;
    for (uint32_t page = first;; page += PAGE_SIZE) {
        uint32_t pde = directory[page >> 22];
        if ((pde & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return false;
        const uint32_t *table =
            (const uint32_t*)(uintptr_t)(pde & 0xFFFFF000U);
        uint32_t pte = table[(page >> 12) & 0x3FFU];
        uint32_t required = PAGE_PRESENT | PAGE_USER |
                            (write_access ? PAGE_RW : 0U);
        if ((pte & required) != required) return false;
        if (page == last) break;
    }
    return true;
}

int copy_from_user(void *destination, const void *user_source, size_t length) {
    if (destination == NULL || user_source == NULL || length == 0 ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_source,
                               length, false)) {
        return -1;
    }
    memcpy(destination, user_source, length);
    return 0;
}

int copy_to_user(void *user_destination, const void *source, size_t length) {
    if (user_destination == NULL || source == NULL || length == 0 ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_destination,
                               length, true)) {
        return -1;
    }
    memcpy(user_destination, source, length);
    return 0;
}

int copy_to_user_space(page_directory_t *pd, uint32_t user_destination,
                       const void *source, size_t length) {
    if (pd == NULL || source == NULL || length == 0 ||
        !user_range_accessible(pd, user_destination, length, true)) {
        return -1;
    }

    const uint8_t *input = (const uint8_t*)source;
    while (length != 0) {
        uint32_t pde = ((uint32_t*)pd->entries)[user_destination >> 22];
        uint32_t *table = (uint32_t*)(uintptr_t)(pde & 0xFFFFF000U);
        uint32_t pte = table[(user_destination >> 12) & 0x3FFU];
        uint32_t page_offset = user_destination & (PAGE_SIZE - 1U);
        size_t amount = PAGE_SIZE - page_offset;
        if (amount > length) amount = length;
        void *physical = (void*)(uintptr_t)((pte & 0xFFFFF000U) + page_offset);
        memcpy(physical, input, amount);
        user_destination += (uint32_t)amount;
        input += amount;
        length -= amount;
    }
    return 0;
}

int copy_string_from_user(char *destination, size_t capacity,
                          const char *user_source) {
    if (destination == NULL || user_source == NULL || capacity == 0) return -1;
    for (size_t index = 0; index < capacity; ++index) {
        char value;
        if (copy_from_user(&value, user_source + index, sizeof(value)) != 0) {
            destination[0] = '\0';
            return -1;
        }
        destination[index] = value;
        if (value == '\0') return (int)index;
    }
    destination[0] = '\0';
    return -1;
}

#define KERNEL_MMIO_MAX_BYTES (64U * 1024U * 1024U)

static void *map_kernel_identity_range(page_directory_t *directory,
                                       uint32_t physical_address,
                                       size_t length, uint32_t flags) {
    if (directory != paging_kernel_directory() || length == 0U ||
        length > KERNEL_MMIO_MAX_BYTES ||
        length > UINT32_MAX - physical_address) return NULL;
    uint32_t final_address = physical_address + (uint32_t)length - 1U;
    if ((physical_address >= USER_BASE && physical_address < USER_TOP) ||
        (final_address >= USER_BASE && final_address < USER_TOP) ||
        (physical_address < USER_BASE && final_address >= USER_BASE))
        return NULL;

    uint32_t first = physical_address & ~(PAGE_SIZE - 1U);
    uint32_t last = final_address & ~(PAGE_SIZE - 1U);
    uint32_t expected_flags = PAGE_PRESENT |
        (flags & (PAGE_RW | PAGE_PAT_INDEX_1 | PAGE_CACHE_DISABLE));
    uint32_t irq_flags = page_table_lock_acquire_irq();
    bool valid = true;
    bool changed = false;

    /* Validate the complete range before publishing any PTE.  Existing exact
     * identity mappings are idempotent.  An uncached device aperture may also
     * retain an already-established write-combining identity page (the early
     * framebuffer does this before display_control_prepare()).  That is the
     * same physical mapping with a stricter device-memory cache policy, not an
     * alias.  All other cache-mode, privilege or physical conflicts fail
     * closed without partially changing the aperture. */
    for (uint32_t page = first;; page += PAGE_SIZE) {
        uint32_t directory_index = page >> 22U;
        uint32_t pde = ((uint32_t *)directory->entries)[directory_index];
        if ((pde & (PAGE_PRESENT | PAGE_USER)) != PAGE_PRESENT) {
            valid = false;
            break;
        }
        uint32_t *table =
            (uint32_t *)(uintptr_t)(pde & 0xFFFFF000U);
        uint32_t existing = table[(page >> 12U) & 0x3FFU] &
            ~(PAGE_ACCESSED | PAGE_DIRTY);
        uint32_t existing_flags = existing & (PAGE_SIZE - 1U);
        bool exact = existing == (page | expected_flags);
        bool compatible_wc =
            (expected_flags & PAGE_CACHE_DISABLE) != 0U &&
            existing == (page | PAGE_PRESENT | PAGE_RW |
                          PAGE_PAT_INDEX_1);
        if (existing != 0U &&
            ((!exact && !compatible_wc) ||
             (existing & ~(PAGE_SIZE - 1U)) != page ||
             (existing_flags & PAGE_USER) != 0U)) {
            valid = false;
            break;
        }
        if (page == last) break;
    }

    if (valid) {
        for (uint32_t page = first;; page += PAGE_SIZE) {
            uint32_t pde = ((uint32_t *)directory->entries)[page >> 22U];
            uint32_t *table =
                (uint32_t *)(uintptr_t)(pde & 0xFFFFF000U);
            uint32_t table_index = (page >> 12U) & 0x3FFU;
            if ((table[table_index] & PAGE_PRESENT) == 0U) {
                table[table_index] = page | expected_flags;
                changed = true;
            }
            if (page == last) break;
        }
        if (changed)
            tlb_shootdown_or_panic(
                directory, first, true);
    }
    page_table_lock_release_irq(irq_flags);
    return valid ? (void *)(uintptr_t)physical_address : NULL;
}

void *map_kernel_mmio(uint32_t physical_address, size_t length) {
    return map_kernel_identity_range(
        paging_kernel_directory(), physical_address, length,
        PAGE_RW | PAGE_CACHE_DISABLE);
}

void *map_kernel_write_combining(uint32_t physical_address, size_t length) {
    if (!prepare_pat_write_combining()) return NULL;
    __asm__ __volatile__("wbinvd" : : : "memory");
    void *mapping = map_kernel_identity_range(
        paging_kernel_directory(), physical_address, length,
        PAGE_RW | PAGE_PAT_INDEX_1);
    __asm__ __volatile__("wbinvd" : : : "memory");
    return mapping;
}

int map_page(page_directory_t *pd, uint32_t virtual_address,
             uint32_t physical_address, uint32_t flags) {
    if (pd == NULL || (virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (PAGE_SIZE - 1U)) != 0) {
        return -1;
    }

    uint32_t irq_flags = page_table_lock_acquire_irq();

    uint32_t directory_index = virtual_address >> 22;
    bool kernel_directory = (void*)pd == (void*)page_directory;
    if (!kernel_directory &&
        (virtual_address < USER_BASE || virtual_address >= USER_TOP ||
         directory_index == LOCAL_APIC_DIRECTORY_INDEX)) {
        page_table_lock_release_irq(irq_flags);
        return -1;
    }
    uint32_t table_index = (virtual_address >> 12) & 0x3FFU;
    uint32_t *directory = (uint32_t*)pd->entries;
    uint32_t *table;

    if ((directory[directory_index] & PAGE_PRESENT) == 0) {
        table = (uint32_t*)allocate_page();
        if (table == NULL) {
            page_table_lock_release_irq(irq_flags);
            return -1;
        }
        directory[directory_index] = (uint32_t)(uintptr_t)table |
                                     PAGE_PRESENT | PAGE_RW |
                                     (flags & PAGE_USER);
    } else {
        table = (uint32_t*)(uintptr_t)(directory[directory_index] & 0xFFFFF000U);
        if ((flags & PAGE_USER) != 0) {
            directory[directory_index] |= PAGE_USER;
        }
        if ((flags & PAGE_RW) != 0) {
            directory[directory_index] |= PAGE_RW;
        }
    }

    uint32_t expected = physical_address | PAGE_PRESENT |
        (flags & (PAGE_RW | PAGE_USER | PAGE_PAT_INDEX_1 |
                  PAGE_CACHE_DISABLE));
    uint32_t existing = table[table_index] & ~(PAGE_ACCESSED | PAGE_DIRTY);
    if (existing != 0U && existing != expected) {
        page_table_lock_release_irq(irq_flags);
        return -1;
    }
    if (existing == expected) {
        page_table_lock_release_irq(irq_flags);
        return 0;
    }
    table[table_index] = expected;
    tlb_shootdown_or_panic(pd, virtual_address, false);
    page_table_lock_release_irq(irq_flags);
    return 0;
}
