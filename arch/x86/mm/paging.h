/**
 * @file arch/x86/mm/paging.h
 * @brief x86-Seitentabellen- und Userbereichsvertrag.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Mappings prüfen Alignment, Rechte und physische/virtuelle Grenzen.
 */
#ifndef PAGING_H
#define PAGING_H

// Control Register flags
#define CR0_PG 0x80000000 // Paging enable
#define CR0_PE 0x00000001 // Protected mode enable

#define PAGE_SIZE 4096U                     // 4 KB pages
#define PAGE_TABLE_ENTRIES 1024U            // 1024 entries per page table
#define PAGE_DIRECTORY_ENTRIES 1024U        // 1024 entries per page directory

#define KERNEL_BASE 0x00000000              // Kernel starts at 0x00000000
/* The complete kernel half below USER_BASE is a shared, supervisor-only
 * direct map.  This raises the managed physical-memory ceiling from 256 MiB
 * to 1 GiB while preserving the existing 1 GiB/2 GiB kernel/user split. */
#define KERNEL_PAGE_ENTRIES 256U             // Identity-map the first 1 GiB
#define KERNEL_IDENTITY_LIMIT (KERNEL_PAGE_ENTRIES * PAGE_TABLE_ENTRIES * PAGE_SIZE)

/* Reserved supervisor-only VA window for fault-contained task and per-CPU
 * idle stacks. Each slot is [guard][8 KiB stack][guard]. The arena preserves
 * all 32 task stacks after up to 15 APs have acquired private idle stacks;
 * one rounded spare slot keeps the fixed layout naturally bounded. The
 * corresponding identity-mapped physical range is reserved before the PMM is
 * initialized so an unmapped guard VA cannot hide an allocatable frame. */
#define KERNEL_STACK_ARENA_BASE 0x3FF00000U
#define KERNEL_STACK_SLOT_SIZE  (4U * PAGE_SIZE)
#define KERNEL_STACK_SLOT_COUNT 48U
#define KERNEL_STACK_ARENA_SIZE (KERNEL_STACK_SLOT_COUNT * KERNEL_STACK_SLOT_SIZE)

#define USER_BASE 0x40000000U               // User address spaces start at 1 GiB
#define USER_TOP  0xC0000000U               // Exclusive upper user-space bound
#define USER_HEAP_BASE (USER_BASE + 8U * 1024U * 1024U)
#define USER_HEAP_TOP  (USER_TOP - 16U * 1024U * 1024U)
#define USER_STACK_SIZE (8U * PAGE_SIZE)
#define USER_STACK_UPPER_GUARD (USER_TOP - PAGE_SIZE)
#define USER_STACK_TOP USER_STACK_UPPER_GUARD
#define USER_STACK_BOTTOM (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_STACK_LOWER_GUARD (USER_STACK_BOTTOM - PAGE_SIZE)
#define USER_PAGE_START (USER_BASE / (PAGE_SIZE * PAGE_TABLE_ENTRIES)) // Start index in the page directory
#define USER_PAGE_END (USER_TOP / (PAGE_SIZE * PAGE_TABLE_ENTRIES))


// Page table/directory attributes
#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4
#define PAGE_PAT_INDEX_1 0x8
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY 0x40
#define X86_TLB_SHOOTDOWN_VECTOR 0xF1U

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
/** Initialize CPU-local PAT during serial bootstrap, before online/tasks.
 * Unsupported CPUs retain UC fallback; heterogeneous PAT profiles fail. */
bool paging_prepare_cpu_memory_types(void);
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
void* map_kernel_write_combining(uint32_t physical_address, size_t length);
int unmap_kernel_page(uint32_t virtual_address, bool free_physical_frame);
bool paging_kernel_page_present(uint32_t virtual_address);
bool paging_is_enabled(void);
void paging_tlb_shootdown_isr(void *frame);
bool paging_tlb_shootdown_probe(void);


#endif // PAGING_H
