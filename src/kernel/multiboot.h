#ifndef MULTIBOOT_H
#define MULTIBOOT_H


#include <stdint.h>
#include <stddef.h>


// Common Multiboot 2 tags
#define MULTIBOOT2_TAG_TYPE_END             0
#define MULTIBOOT2_TAG_TYPE_CMDLINE         1
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER     2
#define MULTIBOOT2_TAG_TYPE_MODULE          3
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO   4
#define MULTIBOOT2_TAG_TYPE_BOOTDEV         5
#define MULTIBOOT2_TAG_TYPE_MMAP            6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER     8
#define MULTIBOOT2_TAG_TYPE_ELF_SECTIONS    9
#define MULTIBOOT2_TAG_TYPE_APM             10
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP        21

// Multiboot 2 information structure passed by the bootloader
typedef struct {
    uint32_t total_size;  // Total size of the Multiboot 2 information structure
    uint32_t reserved;    // Reserved (must be 0)
} __attribute__((packed)) multiboot2_info_t;

// Generic tag structure
typedef struct {
    uint32_t type;        // Tag type
    uint32_t size;        // Size of the tag (including this header)
} __attribute__((packed)) multiboot2_tag_t;

// Specific tag types
typedef struct {
    uint32_t type;
    uint32_t size;
    char string[];        // Command line string
} __attribute__((packed)) multiboot2_tag_string_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;   // Lower memory in KB
    uint32_t mem_upper;   // Upper memory in KB
} __attribute__((packed)) multiboot2_tag_basic_meminfo_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t boot_device; // Boot device
    uint32_t partition;
    uint32_t subpartition;
} __attribute__((packed)) multiboot2_tag_bootdev_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t addr;        // Module physical address
    uint64_t length;      // Module length
    char string[];        // Module name
} __attribute__((packed)) multiboot2_tag_module_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __attribute__((packed)) multiboot2_tag_framebuffer_t;

// Memory map entry structure
typedef struct {
    uint64_t base_addr; // Region base address
    uint64_t length;    // Region length
    uint32_t type;      // Region type
    uint32_t reserved;  // Reserved, must be zero
} multiboot_mmap_entry_t;

// Memory map tag structure
typedef struct {
    uint32_t type;         // Tag type (should be MULTIBOOT_TAG_TYPE_MMAP)
    uint32_t size;         // Total size, including all entries
    uint32_t entry_size;   // Size of each memory map entry (24 bytes)
    uint32_t entry_version;
    multiboot_mmap_entry_t entries[];
} multiboot2_tag_mmap_t;



void enumerate_multiboot2_tags(multiboot2_info_t *mb_info);


#endif // MULTIBOOT_H