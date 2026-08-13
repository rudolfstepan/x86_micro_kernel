#include "arch/x86/mm/paging.h"
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

#define LOCAL_APIC_BASE 0xFEE00000U
#define LOCAL_APIC_DIRECTORY_INDEX (LOCAL_APIC_BASE >> 22)
#define LOCAL_APIC_TABLE_INDEX ((LOCAL_APIC_BASE >> 12) & 0x3FFU)
static uint32_t local_apic_page_table[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static page_directory_t *current_directory;

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
    page_directory_t *pd = (page_directory_t*)allocate_page();
    if (pd == NULL) {
        printf("Failed to allocate page directory.\n");
        return NULL;
    }

    uint32_t *entries = (uint32_t*)pd->entries;
    /* Every address space shares supervisor-only kernel mappings.  This also
     * includes device MMIO above USER_TOP (PCI BARs, LAPIC, and similar), so
     * hardware IRQ handlers remain usable while a Ring-3 CR3 is active. */
    for (size_t i = 0; i < PAGE_DIRECTORY_ENTRIES; ++i) {
        if (i < USER_PAGE_START || i >= USER_PAGE_END) {
            entries[i] = page_directory[i];
        }
    }
    return pd;
}

page_directory_t *paging_kernel_directory(void) {
    return (page_directory_t*)(void*)page_directory;
}

page_directory_t *paging_current_directory(void) {
    return current_directory ? current_directory : paging_kernel_directory();
}

void switch_page_directory(page_directory_t *pd) {
    if (pd == NULL) pd = paging_kernel_directory();
    current_directory = pd;
    load_cr3((uint32_t)(uintptr_t)pd);
}

void init_paging(void) {
    memset(page_directory, 0, sizeof(page_directory));
    memset(kernel_page_tables, 0, sizeof(kernel_page_tables));
    memset(local_apic_page_table, 0, sizeof(local_apic_page_table));

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

    /* LAPIC MMIO sits near 4 GB and needs an uncached supervisor mapping. */
    local_apic_page_table[LOCAL_APIC_TABLE_INDEX] =
        LOCAL_APIC_BASE | PAGE_PRESENT | PAGE_RW | PAGE_CACHE_DISABLE;
    page_directory[LOCAL_APIC_DIRECTORY_INDEX] =
        (uint32_t)(uintptr_t)local_apic_page_table |
        PAGE_PRESENT | PAGE_RW;

    switch_page_directory(paging_kernel_directory());
    write_cr0(read_cr0() | CR0_PG);
    printf("Paging enabled; identity-mapped the first %u MB.\n",
           (unsigned int)(KERNEL_IDENTITY_LIMIT / 1024U / 1024U));
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
}

int unmap_page(page_directory_t *pd, uint32_t virtual_address,
               bool free_physical_frame) {
    if (pd == NULL || virtual_address < USER_BASE ||
        virtual_address >= USER_TOP ||
        (virtual_address & (PAGE_SIZE - 1U)) != 0) return -1;
    uint32_t *directory = (uint32_t*)pd->entries;
    uint32_t di = virtual_address >> 22;
    uint32_t ti = (virtual_address >> 12) & 0x3FFU;
    if ((directory[di] & PAGE_PRESENT) == 0) return -1;
    uint32_t *table = (uint32_t*)(uintptr_t)(directory[di] & 0xFFFFF000U);
    if ((table[ti] & PAGE_PRESENT) == 0) return -1;
    if (free_physical_frame) {
        free_page((void*)(uintptr_t)(table[ti] & 0xFFFFF000U));
    }
    table[ti] = 0;
    if (pd == paging_current_directory()) flush_tlb();
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

void *map_kernel_mmio(uint32_t physical_address, size_t length) {
    if (length == 0 || length > UINT32_MAX - physical_address) return NULL;

    /* Identity-mapped MMIO inside the user virtual window would alias a
     * process-private PDE and disappear (or become user-accessible) on CR3
     * switches.  A dedicated high-MMIO virtual allocator is required before
     * such BARs can be supported safely. */
    uint32_t final_address = physical_address + (uint32_t)length - 1U;
    if ((physical_address >= USER_BASE && physical_address < USER_TOP) ||
        (final_address >= USER_BASE && final_address < USER_TOP) ||
        (physical_address < USER_BASE && final_address >= USER_BASE)) {
        return NULL;
    }

    uint32_t first = physical_address & ~(PAGE_SIZE - 1U);
    uint32_t end_address = final_address;
    uint32_t last = end_address & ~(PAGE_SIZE - 1U);

    for (uint32_t page = first;; page += PAGE_SIZE) {
        if (map_page(paging_kernel_directory(), page, page,
                     PAGE_RW | PAGE_CACHE_DISABLE) != 0) {
            return NULL;
        }
        if (page == last) break;
        if (page > UINT32_MAX - PAGE_SIZE) return NULL;
    }
    return (void*)(uintptr_t)physical_address;
}

int map_page(page_directory_t *pd, uint32_t virtual_address,
             uint32_t physical_address, uint32_t flags) {
    if (pd == NULL || (virtual_address & (PAGE_SIZE - 1U)) != 0 ||
        (physical_address & (PAGE_SIZE - 1U)) != 0) {
        return -1;
    }

    uint32_t directory_index = virtual_address >> 22;
    bool kernel_directory = (void*)pd == (void*)page_directory;
    if (!kernel_directory &&
        (virtual_address < USER_BASE || virtual_address >= USER_TOP ||
         directory_index == LOCAL_APIC_DIRECTORY_INDEX)) {
        return -1;
    }
    uint32_t table_index = (virtual_address >> 12) & 0x3FFU;
    uint32_t *directory = (uint32_t*)pd->entries;
    uint32_t *table;

    if ((directory[directory_index] & PAGE_PRESENT) == 0) {
        table = (uint32_t*)allocate_page();
        if (table == NULL) {
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

    table[table_index] = physical_address | PAGE_PRESENT |
                         (flags & (PAGE_RW | PAGE_USER | PAGE_CACHE_DISABLE));
    flush_tlb();
    return 0;
}
