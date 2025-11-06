#ifndef MBHEADER_H
#define MBHEADER_H

#include <stdint.h>

// Multiboot1 header magic value defined by the specification
#define MULTIBOOT1_MAGIC 0x1BADB002

// Multiboot1 information flags
#define MULTIBOOT1_FLAG_MEM         0x001  // Indicates mem_lower and mem_upper are valid
#define MULTIBOOT1_FLAG_BOOT_DEVICE 0x002  // Indicates boot_device is valid
#define MULTIBOOT1_FLAG_CMDLINE     0x004  // Indicates cmdline is valid
#define MULTIBOOT1_FLAG_MODS        0x008  // Indicates mods_count and mods_addr are valid
#define MULTIBOOT1_FLAG_AOUT        0x010  // Indicates syms[] is used (a.out symbols)
#define MULTIBOOT1_FLAG_ELF         0x020  // Indicates syms[] is used (ELF section headers)
#define MULTIBOOT1_FLAG_MMAP        0x040  // Indicates mmap_length and mmap_addr are valid
#define MULTIBOOT1_FLAG_DRIVES      0x080  // Indicates drives_length and drives_addr are valid
#define MULTIBOOT1_FLAG_CONFIG      0x100  // Indicates config_table is valid
#define MULTIBOOT1_FLAG_BOOTLOADER  0x200  // Indicates boot_loader_name is valid
#define MULTIBOOT1_FLAG_APM         0x400  // Indicates apm_table is valid
#define MULTIBOOT1_FLAG_VBE         0x800  // Indicates VBE info is valid


// Multiboot1 Header Structure
typedef struct {
    uint32_t magic;          // Must be MULTIBOOT1_MAGIC
    uint32_t flags;          // Feature flags
    uint32_t checksum;       // Header checksum
    uint32_t header_addr;    // Address of the Multiboot header
    uint32_t load_addr;      // Load address of the kernel
    uint32_t load_end_addr;  // End address of the loaded image
    uint32_t bss_end_addr;   // End address of the BSS
    uint32_t entry_addr;     // Kernel entry point address
} __attribute__((packed)) multiboot1_header_t;

// Multiboot1 Information Structure
typedef struct {
    uint32_t flags;             // Flags indicating which fields are valid
    uint32_t mem_lower;         // Amount of lower memory (in KB)
    uint32_t mem_upper;         // Amount of upper memory (in KB)
    uint32_t boot_device;       // Boot device ID
    uint32_t cmdline;           // Address of the command-line string
    uint32_t mods_count;        // Number of modules
    uint32_t mods_addr;         // Address of the module structure array
    uint32_t syms[4];           // Symbols (ELF or a.out specifics)
    uint32_t mmap_length;       // Length of the memory map
    uint32_t mmap_addr;         // Address of the memory map
    uint32_t drives_length;     // Length of the drives structure
    uint32_t drives_addr;       // Address of the drives structure
    uint32_t config_table;      // Address of the ROM configuration table
    uint32_t boot_loader_name;  // Address of the bootloader name
    uint32_t apm_table;         // Address of the APM table
    uint32_t vbe_control_info;  // Address of VBE control information
    uint32_t vbe_mode_info;     // Address of VBE mode information
    uint16_t vbe_mode;          // Current VBE mode
    uint16_t vbe_interface_seg; // VBE interface segment
    uint16_t vbe_interface_off; // VBE interface offset
    uint16_t vbe_interface_len; // VBE interface length
    uint64_t framebuffer_addr;  // Framebuffer physical address
    uint32_t framebuffer_pitch; // Framebuffer pitch (bytes per line)
    uint32_t framebuffer_width; // Framebuffer width in pixels
    uint32_t framebuffer_height;// Framebuffer height in pixels
    uint8_t  framebuffer_bpp;   // Bits per pixel
    uint8_t  framebuffer_type;  // Framebuffer type (0=indexed, 1=RGB, 2=text)
    uint8_t  color_info[6];     // Color type info
} __attribute__((packed)) multiboot1_info_t;

// Multiboot1 Module Structure
typedef struct {
    uint32_t mod_start;     // Start address of the module
    uint32_t mod_end;       // End address of the module
    uint32_t string;        // Address of the command-line string
    uint32_t reserved;      // Reserved, must be zero
} __attribute__((packed)) multiboot1_module_t;

// Multiboot1 Memory Map Entry
typedef struct {
    uint32_t size;          // Size of the entry (not counting this field)
    uint64_t base_addr;     // Base address of the memory region
    uint64_t length;        // Length of the memory region
    uint32_t type;          // Type of the memory region (1 = usable, others = reserved)
} __attribute__((packed)) multiboot1_mmap_entry_t;

// Multiboot2 Structures (unchanged from your original)
#define MULTIBOOT2_MAGIC 0x36d76289
#define MULTIBOOT2_INFO_TAG_END 0
#define MULTIBOOT2_INFO_TAG_CMDLINE 1
#define MULTIBOOT2_INFO_TAG_BOOT_LOADER_NAME 2
#define MULTIBOOT2_INFO_TAG_MODULE 3
#define MULTIBOOT2_INFO_TAG_BASIC_MEMINFO 4
#define MULTIBOOT2_INFO_TAG_MMAP 6
#define MULTIBOOT2_INFO_TAG_FRAMEBUFFER 8
#define MULTIBOOT2_INFO_TAG_ELF_SECTIONS 9
#define MULTIBOOT2_INFO_TAG_APM 10
#define MULTIBOOT2_INFO_TAG_EFI_MMAP 21

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t descriptor_size;
    uint32_t descriptor_version;
    uint8_t efi_memory_map[];
} __attribute__((packed)) multiboot2_tag_efi_mmap_t;

typedef struct {
    uint32_t type;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t num_pages;
    uint64_t attribute;
} __attribute__((packed)) efi_memory_descriptor_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) multiboot2_mmap_entry_t;

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    const char *cmdline;
    uint32_t reserved;
} __attribute__((packed)) multiboot2_module_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot2_tag_t;

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
    multiboot2_tag_t tags[];
} __attribute__((packed)) multiboot2_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed)) multiboot2_tag_basic_meminfo_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot2_mmap_entry_t entries[];
} __attribute__((packed)) multiboot2_tag_mmap_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    char cmdline[];
} __attribute__((packed)) multiboot2_tag_cmdline_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    char name[];
} __attribute__((packed)) multiboot2_tag_boot_loader_name_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot2_tag_end_t;

#endif // MBHEADER_H
