#ifndef PAGING_H
#define PAGING_H

// Control Register flags
#define CR0_PG 0x80000000 // Paging enable
#define CR0_PE 0x00000001 // Protected mode enable

#define PAGE_SIZE 4096                      // 4 KB pages
#define PAGE_TABLE_ENTRIES 1024             // 1024 entries per page table
#define PAGE_DIRECTORY_ENTRIES 1024         // 1024 entries per page directory

#define KERNEL_BASE 0x00000000              // Kernel starts at 0x00000000
#define KERNEL_PAGE_ENTRIES 256             // Kernel occupies the first 256 page directory entries (1 GB)

#define USER_BASE 0x400000                  // User space starts at 4 MB
#define USER_PAGE_START (USER_BASE / (PAGE_SIZE * PAGE_TABLE_ENTRIES)) // Start index in the page directory


// Page table/directory attributes
#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4

#include <stdint.h>


typedef struct page_table_entry {
    uint32_t present   : 1; // Page present in memory
    uint32_t rw        : 1; // Read/write (0 = read-only, 1 = read/write)
    uint32_t user      : 1; // User/supervisor (0 = supervisor only, 1 = user-mode allowed)
    uint32_t reserved  : 2; // Reserved
    uint32_t accessed  : 1; // Accessed (set by CPU)
    uint32_t dirty     : 1; // Dirty (set by CPU on write)
    uint32_t unused    : 2; // Unused bits
    uint32_t frame     : 20; // Physical frame address (aligned to 4 KB)
} page_table_entry_t;

typedef struct page_table {
    page_table_entry_t entries[1024]; // Array of 1024 page table entries
} page_table_t;

typedef struct page_directory_entry {
    uint32_t present   : 1; // Page table present in memory
    uint32_t rw        : 1; // Read/write
    uint32_t user      : 1; // User/supervisor
    uint32_t reserved  : 2; // Reserved
    uint32_t accessed  : 1; // Accessed
    uint32_t dirty     : 1; // Dirty
    uint32_t unused    : 2; // Unused bits
    uint32_t table     : 20; // Address of the page table (aligned to 4 KB)
} page_directory_entry_t;

typedef struct page_directory {
    page_directory_entry_t entries[1024]; // Array of 1024 page directory entries
} page_directory_t;


void init_paging();
void test_paging();
void page_fault_handler();
page_directory_t* create_page_directory();
void free_page_directory(page_directory_t* pd);


#endif // PAGING_H