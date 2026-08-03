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

uint32_t page_directory[PAGE_DIRECTORY_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t kernel_page_tables[KERNEL_PAGE_ENTRIES][PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

#define LOCAL_APIC_BASE 0xFEE00000U
#define LOCAL_APIC_DIRECTORY_INDEX (LOCAL_APIC_BASE >> 22)
#define LOCAL_APIC_TABLE_INDEX ((LOCAL_APIC_BASE >> 12) & 0x3FFU)
static uint32_t local_apic_page_table[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

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
    for (size_t i = 0; i < KERNEL_PAGE_ENTRIES; ++i) {
        entries[i] = page_directory[i];
    }
    entries[LOCAL_APIC_DIRECTORY_INDEX] =
        page_directory[LOCAL_APIC_DIRECTORY_INDEX];
    return pd;
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

    load_cr3((uint32_t)(uintptr_t)page_directory);
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
    for (size_t i = USER_PAGE_START; i < PAGE_DIRECTORY_ENTRIES; ++i) {
        if (i == LOCAL_APIC_DIRECTORY_INDEX) {
            continue;
        }
        if ((entries[i] & PAGE_PRESENT) != 0) {
            free_page((void*)(uintptr_t)(entries[i] & 0xFFFFF000U));
        }
    }
    free_page(pd);
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
        (virtual_address < USER_BASE ||
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
