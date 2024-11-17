#ifndef MBHEADER_H
#define MBHEADER_H

#include <stdint.h>


// multiboot 1 

// Multiboot header magic value defined by the specification
#define MULTIBOOT_MAGIC 0x2BADB002

// Multiboot header flags
#define MULTIBOOT_FLAG_MEM 0x001
#define MULTIBOOT_FLAG_BOOT_DEVICE 0x002
#define MULTIBOOT_FLAG_CMDLINE 0x004
#define MULTIBOOT_FLAG_MODS 0x008

// Define structures for Multiboot
typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    const char *string;
    uint32_t reserved;
} multiboot_module_t;

typedef struct {
    uint32_t size;
    uint32_t base_addr_low;
    uint32_t base_addr_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    const char *cmdline;
    uint32_t mods_count;
    multiboot_module_t *mods_addr;
    uint32_t mmap_length;
    multiboot_mmap_entry_t *mmap_addr;
} __attribute__((packed)) multiboot_info_t;



#endif // MBHEADER_H