#ifndef SYSTEM_H
#define SYSTEM_H


#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define NUM_SYSCALLS 1

// system call numbers
// #define SYSCALL_PRINT 0
// #define SYSCALL_MALLOC 1
// #define SYSCALL_FREE 2

#define SYSCALL_SLEEP 0

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define PROGRAM_LOAD_ADDRESS 0x10000 // default address where the program will be loaded into memory except in the case of a program header
#define BUFFER_SIZE 256

// #define PIT_FREQ 1193180
// #define SPEAKER_PORT 0x61
// #define PIT_CMD_PORT 0x43
// #define PIT_CHANNEL_0 0x40

// for memory dump
#define BYTES_PER_LINE 16
#define MAX_LINES 20


// Multiboot structure definitions
typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} MemoryMapEntry;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t shndx;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    // ... other Multiboot fields ...
} MultibootInfo;

extern MultibootInfo* sys_mb_info;

// syscalls
void initialize_syscall_table();
void syscall_sleep(int ticks);

// utility functions
void memory_dump(uint32_t start_address, uint32_t end_address);
void print_memory_map(const MultibootInfo* mb_info);

#endif