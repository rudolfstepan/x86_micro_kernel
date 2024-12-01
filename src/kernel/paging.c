#include "paging.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "toolchain/stdio.h"

// Global variables
uint32_t page_directory[PAGE_DIRECTORY_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
uint32_t page_table[PAGE_TABLE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));


#define PAGE_SIZE 4096 // 4 KB
#define MEMORY_POOL_SIZE (1024 * 1024 * 16) // Example: 16 MB memory pool

// Memory pool for the page allocator
uint8_t memory_pool[MEMORY_POOL_SIZE] __attribute__((aligned(PAGE_SIZE)));
uint32_t free_page_bitmap[MEMORY_POOL_SIZE / PAGE_SIZE / 32] = {0}; // Bitmap for tracking free pages

// Helper: Find the first free page in the bitmap
static int find_free_page() {
    for (size_t i = 0; i < (MEMORY_POOL_SIZE / PAGE_SIZE / 32); i++) {
        if (free_page_bitmap[i] != 0xFFFFFFFF) { // If there are free pages in this block
            for (int bit = 0; bit < 32; bit++) {
                if (!(free_page_bitmap[i] & (1 << bit))) { // Check if the bit is free
                    return i * 32 + bit; // Return the index of the free page
                }
            }
        }
    }
    return -1; // No free pages
}

// Helper: Mark a page as used in the bitmap
static void mark_page_used(int page_index) {
    free_page_bitmap[page_index / 32] |= (1 << (page_index % 32));
}

// Helper: Mark a page as free in the bitmap
static void mark_page_free(int page_index) {
    free_page_bitmap[page_index / 32] &= ~(1 << (page_index % 32));
}

// Allocate a single 4 KB page
void* allocate_page() {
    int free_page = find_free_page();
    if (free_page == -1) {
        return NULL; // No free pages available
    }

    mark_page_used(free_page);
    return (void*)&memory_pool[free_page * PAGE_SIZE];
}

// Free a single 4 KB page
void free_page(void* page) {
    uintptr_t address = (uintptr_t)page;
    if (address < (uintptr_t)memory_pool || address >= (uintptr_t)(memory_pool + MEMORY_POOL_SIZE)) {
        return; // Address is outside the memory pool, ignore
    }

    int page_index = (address - (uintptr_t)memory_pool) / PAGE_SIZE;
    mark_page_free(page_index);
}


// Helper: Load CR3 (Page Directory Base Register)
static inline void load_cr3(uint32_t address) {
    asm volatile("mov %0, %%cr3" : : "r"(address));
}

// Helper: Read CR0
static inline uint32_t read_cr0() {
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

// Helper: Write CR0
static inline void write_cr0(uint32_t cr0) {
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

// Helper: Flush TLB
static inline void flush_tlb() {
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
}

page_directory_t* create_page_directory() {
    page_directory_t* pd = allocate_page(); // Allocate one page for the directory
    if (!pd) {
        printf("Failed to allocate page directory.\n");
        return NULL;
    }

    memset(pd, 0, PAGE_SIZE);

    // Map the kernel space (identity mapping or higher-half kernel)
    // for (int i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    //     pd->entries[i] = page_directory->entries[i];
    // }

    return pd;
}


// Initialize paging
void init_paging() {
    // 1. Clear page directory and page table
    memset(page_directory, 0, sizeof(page_directory));
    memset(page_table, 0, sizeof(page_table));

    // 2. Identity map the first 4 MB (kernel space)
    for (uint32_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
        page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW; // Physical address
    }

    // 3. Add the page table to the page directory
    page_directory[0] = ((uint32_t)page_table) | PAGE_PRESENT | PAGE_RW;

    // 4. Load the page directory into CR3
    load_cr3((uint32_t)page_directory);

    // 5. Enable paging in CR0
    uint32_t cr0 = read_cr0();
    cr0 |= CR0_PG; // Set the PG (Paging) bit
    write_cr0(cr0);

    // Paging is now enabled
    printf("Paging enabled successfully.\n");
}

// Test paging by accessing memory
void test_paging() {
    volatile uint32_t* test_address = (uint32_t*)0x1000; // Identity-mapped address
    *test_address = 42; // Should not cause a crash
    printf("Paging test successful: %d\n", *test_address);
}

void free_page_directory(page_directory_t* pd) {
    if (!pd) return;

    // Free all user page tables
    for (int i = USER_PAGE_START; i < PAGE_DIRECTORY_ENTRIES; i++) {
        uint32_t entry = *(uint32_t*)&pd->entries[i]; // Cast entry to uint32_t
        if (entry & PAGE_PRESENT) {                  // Check if present
            uint32_t* pt = (uint32_t*)(entry & ~0xFFF); // Get physical address of page table
            free_page(pt);                           // Free the page table
        }
    }

    free_page(pd); // Free the page directory itself
}

void map_page(page_directory_t* pd, uint32_t virtual_address, uint32_t physical_address, uint32_t flags) {
    uint32_t dir_index = (virtual_address >> 22) & 0x3FF; // Top 10 bits
    uint32_t table_index = (virtual_address >> 12) & 0x3FF; // Next 10 bits

    page_table_t* pt;
    if (!pd->entries[dir_index].present) {
        // Allocate a new page table
        pt = (page_table_t*)allocate_page();
        if (!pt) {
            printf("Failed to allocate page table.\n");
            return;
        }
        memset(pt, 0, sizeof(page_table_t));
        pd->entries[dir_index].table = ((uint32_t)pt) >> 12;
        pd->entries[dir_index].present = 1;
        pd->entries[dir_index].rw = (flags & 0x2) >> 1;
        pd->entries[dir_index].user = (flags & 0x4) >> 2;
    } else {
        pt = (page_table_t*)((pd->entries[dir_index].table) << 12);
    }

    // Set the page table entry
    pt->entries[table_index].frame = (physical_address >> 12);
    pt->entries[table_index].present = 1;
    pt->entries[table_index].rw = (flags & 0x2) >> 1;
    pt->entries[table_index].user = (flags & 0x4) >> 2;

    flush_tlb(); // Flush TLB after modifying page tables
}

