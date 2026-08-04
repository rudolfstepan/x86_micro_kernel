#ifndef PAGING_H
#define PAGING_H

// Control Register flags
#define CR0_PG 0x80000000 // Paging enable
#define CR0_PE 0x00000001 // Protected mode enable

#define PAGE_SIZE 4096                      // 4 KB pages
#define PAGE_TABLE_ENTRIES 1024             // 1024 entries per page table
#define PAGE_DIRECTORY_ENTRIES 1024         // 1024 entries per page directory

#define KERNEL_BASE 0x00000000              // Kernel starts at 0x00000000
#define KERNEL_PAGE_ENTRIES 64              // Identity-map the first 256 MB
#define KERNEL_IDENTITY_LIMIT (KERNEL_PAGE_ENTRIES * PAGE_TABLE_ENTRIES * PAGE_SIZE)

#define USER_BASE 0x40000000U               // User address spaces start at 1 GiB
#define USER_TOP  0xC0000000U               // Exclusive upper user-space bound
#define USER_HEAP_BASE (USER_BASE + 8U * 1024U * 1024U)
#define USER_HEAP_TOP  (USER_TOP - 16U * 1024U * 1024U)
#define USER_PAGE_START (USER_BASE / (PAGE_SIZE * PAGE_TABLE_ENTRIES)) // Start index in the page directory
#define USER_PAGE_END (USER_TOP / (PAGE_SIZE * PAGE_TABLE_ENTRIES))


// Page table/directory attributes
#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4
#define PAGE_CACHE_DISABLE 0x10

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct page_table_entry {
    uint32_t present   : 1; // Page present in memory
    uint32_t rw        : 1; // Read/write (0 = read-only, 1 = read/write)
    uint32_t user      : 1; // User/supervisor (0 = supervisor only, 1 = user-mode allowed)
    uint32_t write_through : 1;
    uint32_t cache_disable : 1;
    uint32_t accessed  : 1; // Accessed (set by CPU)
    uint32_t dirty     : 1; // Dirty (set by CPU on write)
    uint32_t pat       : 1;
    uint32_t global    : 1;
    uint32_t available : 3;
    uint32_t frame     : 20; // Physical frame address (aligned to 4 KB)
} page_table_entry_t;

typedef struct page_table {
    page_table_entry_t entries[1024]; // Array of 1024 page table entries
} page_table_t;

typedef struct page_directory_entry {
    uint32_t present   : 1; // Page table present in memory
    uint32_t rw        : 1; // Read/write
    uint32_t user      : 1; // User/supervisor
    uint32_t write_through : 1;
    uint32_t cache_disable : 1;
    uint32_t accessed  : 1; // Accessed
    uint32_t ignored   : 1;
    uint32_t page_size : 1;
    uint32_t global    : 1;
    uint32_t available : 3;
    uint32_t table     : 20; // Address of the page table (aligned to 4 KB)
} page_directory_entry_t;

typedef struct page_directory {
    page_directory_entry_t entries[1024]; // Array of 1024 page directory entries
} page_directory_t;


void init_paging(void);
void test_paging(void);
page_directory_t* create_page_directory(void);
void free_page_directory(page_directory_t* pd);
int map_page(page_directory_t* pd, uint32_t virtual_address,
             uint32_t physical_address, uint32_t flags);
int unmap_page(page_directory_t* pd, uint32_t virtual_address,
               bool free_physical_frame);
void switch_page_directory(page_directory_t* pd);
page_directory_t* paging_kernel_directory(void);
page_directory_t* paging_current_directory(void);
bool user_range_accessible(const page_directory_t* pd, uint32_t address,
                           size_t length, bool write_access);
int copy_from_user(void* destination, const void* user_source, size_t length);
int copy_to_user(void* user_destination, const void* source, size_t length);
int copy_to_user_space(page_directory_t* pd, uint32_t user_destination,
                       const void* source, size_t length);
int copy_string_from_user(char* destination, size_t capacity,
                          const char* user_source);
void* map_kernel_mmio(uint32_t physical_address, size_t length);


#endif // PAGING_H
