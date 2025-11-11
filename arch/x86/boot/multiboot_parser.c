/**
 * @file multiboot.c
 * @brief Multiboot 1 and 2 information parsing
 * 
 * Parses bootloader-provided information structures to extract
 * memory maps, module information, and boot parameters.
 */

#include <stdbool.h>
#include <stdint.h>
#include "../arch/x86/include/mbheader.h"
#include "lib/libc/stdio.h"

extern uint64_t total_memory;  // Global memory counter

//---------------------------------------------------------------------------------------------
// Multiboot 1 Parsing
//---------------------------------------------------------------------------------------------

/**
 * Parse and display Multiboot1 information structure
 * Extracts memory info, boot device, modules, and memory map
 * 
 * @param mb_info Pointer to Multiboot1 info structure from bootloader
 */
void parse_multiboot1_info(const multiboot1_info_t *mb_info) {
    printf("Parsing Multiboot1 Information...\n");

    // Check flags for available fields
    if (mb_info->flags & MULTIBOOT1_FLAG_MEM) {
        printf("Memory Information: ");
        printf("  Lower Memory: %u KB, ", mb_info->mem_lower);
        printf("  Upper Memory: %u KB\n", mb_info->mem_upper);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_BOOT_DEVICE) {
        printf("Boot Device: %p\n", mb_info->boot_device);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_CMDLINE) {
        const char *cmdline = (const char *)mb_info->cmdline;
        printf("Command Line: %s\n", cmdline);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_MODS) {
        printf("Modules:\n");
        const multiboot1_module_t *mods = (const multiboot1_module_t *)mb_info->mods_addr;
        for (uint32_t i = 0; i < mb_info->mods_count; i++) {
            printf("  Module %u:\n", i + 1);
            printf("    Start Address: 0x%x\n", mods[i].mod_start);
            printf("    End Address: 0x%x\n", mods[i].mod_end);
            const char *mod_cmdline = (const char *)mods[i].string;
            printf("    Command Line: %s\n", mod_cmdline ? mod_cmdline : "(none)");
        }
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_MMAP) {
        printf("Memory Map:\n");
        const multiboot1_mmap_entry_t *mmap = (const multiboot1_mmap_entry_t *)mb_info->mmap_addr;
        const uint8_t *mmap_end = (const uint8_t *)mb_info->mmap_addr + mb_info->mmap_length;

        printf("------------------------------------------------------------\n");
        printf("| Address                 | Length        | Type (1=Usable)|\n");
        printf("------------------------------------------------------------\n");

        while ((uint8_t *)mmap < mmap_end) {
            printf("| %-12p ", mmap->base_addr);
            printf("| %-12p | ", mmap->base_addr + mmap->length - 1);
            printf("%-13u | ", mmap->length);
            printf("%-14u |\n", mmap->type);

            // Only count usable memory (type == 1)
            if (mmap->type == 1) {
                total_memory += mmap->length;
            }

            // Advance to the next entry
            mmap = (const multiboot1_mmap_entry_t *)((uint8_t *)mmap + mmap->size + sizeof(mmap->size));
        }
        printf("------------------------------------------------------------\n");
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_BOOTLOADER) {
        const char *bootloader_name = (const char *)mb_info->boot_loader_name;
        printf("Bootloader Name: %s\n", bootloader_name);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_VBE) {
        printf("VBE Information:\n");
        printf("Control Info: %p ", mb_info->vbe_control_info);
        printf("Mode Info: %p ", mb_info->vbe_mode_info);
        printf("Mode: %p\n", mb_info->vbe_mode);
        printf("Interface Segment: %p ", mb_info->vbe_interface_seg);
        printf("Offset: %p ", mb_info->vbe_interface_off);
        printf("Length: %u\n", mb_info->vbe_interface_len);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_APM) {
        printf("APM Table Address: 0x%x\n", mb_info->apm_table);
    }

    printf("Parsing Complete.\n");
}

//---------------------------------------------------------------------------------------------
// Multiboot 2 Parsing (EFI Support)
//---------------------------------------------------------------------------------------------

/**
 * Print EFI memory map from Multiboot2 structure
 * 
 * @param mb_info Pointer to Multiboot2 info structure
 */
void print_efi_memory_map(const multiboot2_info_t *mb_info) {
    const multiboot2_tag_t *tag = mb_info->tags;

    // Iterate through all Multiboot2 tags
    while (tag->type != MULTIBOOT2_INFO_TAG_END) {
        if (tag->type == MULTIBOOT2_INFO_TAG_EFI_MMAP) {
            const multiboot2_tag_efi_mmap_t *efi_mmap_tag = (const multiboot2_tag_efi_mmap_t *)tag;
            const efi_memory_descriptor_t *desc = (const efi_memory_descriptor_t *)efi_mmap_tag->efi_memory_map;
            uint8_t *end = (uint8_t *)efi_mmap_tag + efi_mmap_tag->size;

            printf("EFI Memory Map:\n");
            printf("-------------------------------------------------------------\n");
            printf("| Type | Physical Start | Number of Pages | Attributes      |\n");
            printf("-------------------------------------------------------------\n");

            // Iterate over the descriptors
            while ((uint8_t *)desc + efi_mmap_tag->descriptor_size <= end) {
                printf("| %4u | 0x%013llx | %15llu | 0x%016llx |\n",
                       desc->type,
                       desc->physical_start,
                       desc->num_pages,
                       desc->attribute);

                // Advance to the next descriptor
                desc = (const efi_memory_descriptor_t *)((uint8_t *)desc + efi_mmap_tag->descriptor_size);
            }

            printf("-------------------------------------------------------------\n");

            // Debugging information
            printf("Debug: EFI MMap tag size: %u, Descriptor size: %u\n",
                   efi_mmap_tag->size, efi_mmap_tag->descriptor_size);
        }
        
        // Move to the next tag
        tag = (const multiboot2_tag_t *)((uint8_t *)tag + tag->size);
    }
}

/**
 * Parse Multiboot2 information tags
 * 
 * @param mb_info Pointer to Multiboot2 info structure
 */
void parse_multiboot2_info(const multiboot2_info_t *mb_info) {
    multiboot2_tag_t *tag = (multiboot2_tag_t *)(mb_info->tags);

    while (tag->type != MULTIBOOT2_INFO_TAG_END) {
        switch (tag->type) {
            case MULTIBOOT2_INFO_TAG_CMDLINE:
                printf("Command Line: %s\n", ((multiboot2_tag_cmdline_t *)tag)->cmdline);
                break;
            case MULTIBOOT2_INFO_TAG_BOOT_LOADER_NAME:
                printf("Bootloader Name: %s\n", ((multiboot2_tag_boot_loader_name_t *)tag)->name);
                break;
            case MULTIBOOT2_INFO_TAG_BASIC_MEMINFO:
                printf("Basic Memory Info: Lower = %u KB, Upper = %u KB\n",
                       ((multiboot2_tag_basic_meminfo_t *)tag)->mem_lower,
                       ((multiboot2_tag_basic_meminfo_t *)tag)->mem_upper);
                break;
            case MULTIBOOT2_INFO_TAG_MMAP:
                printf("Memory Map available\n");
                break;
            case MULTIBOOT2_INFO_TAG_MODULE:
                printf("Module available\n");
                break;
            case MULTIBOOT2_INFO_TAG_EFI_MMAP:
                print_efi_memory_map(mb_info);
                break;
            default:
                printf("Unknown tag type: %u\n", tag->type);
                break;
        }
        tag = (multiboot2_tag_t *)((uint8_t *)tag + tag->size);
    }
}

/**
 * Compute total usable memory from Multiboot2 memory map
 * 
 * @param mb_info Pointer to Multiboot2 info structure
 * @return Total usable memory in bytes
 */
uint64_t compute_total_memory(const multiboot2_info_t *mb_info) {
    uint64_t total = 0;

    // Start parsing the tags
    const multiboot2_tag_t *tag = (const multiboot2_tag_t *)(mb_info->tags);
    while (tag->type != MULTIBOOT2_INFO_TAG_END) {
        if (tag->type == MULTIBOOT2_INFO_TAG_MMAP) {
            const multiboot2_tag_mmap_t *mmap_tag = (const multiboot2_tag_mmap_t *)tag;
            const multiboot2_mmap_entry_t *mmap_entry = mmap_tag->entries;

            printf("Memory map available:\n");
            printf("Entry size: %u\n", mmap_tag->entry_size);

            // Iterate over all memory map entries
            while ((uint8_t *)mmap_entry < (uint8_t *)mmap_tag + mmap_tag->size) {
                if (mmap_entry->type == 1) { // Usable memory
                    total += mmap_entry->length;
                }
                mmap_entry = (const multiboot2_mmap_entry_t *)((uint8_t *)mmap_entry + mmap_tag->entry_size);
            }
        }

        // Move to the next tag
        tag = (const multiboot2_tag_t *)((uint8_t *)tag + tag->size);
    }

    return total;
}
