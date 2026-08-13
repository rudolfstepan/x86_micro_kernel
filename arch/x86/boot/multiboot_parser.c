/**
 * @file multiboot.c
 * @brief Multiboot 1 and 2 information parsing
 * 
 * Parses bootloader-provided information structures to extract
 * memory maps, module information, and boot parameters.
 */

#include <stdbool.h>
#include <stdint.h>
#include "arch/x86/include/mbheader.h"
#include "lib/libc/stdio.h"
#include "mm/kmalloc.h"

static bool reserve_boot_region(uint64_t base, uint64_t length,
                                const char *description) {
    if (length == 0 || memory_reserve_region(base, length) == 0) return true;
    printf("Fatal: unable to reserve %s in the physical memory map.\n",
           description);
    memory_map_reset();
    return false;
}

static bool add_boot_usable_region(uint64_t base, uint64_t length) {
    if (length == 0 || memory_add_usable_region(base, length) == 0) return true;
    printf("Fatal: usable memory-map regions exceed capacity.\n");
    memory_map_reset();
    return false;
}

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
    memory_map_reset();
    if (!reserve_boot_region((uint32_t)(uintptr_t)mb_info, sizeof(*mb_info),
                             "Multiboot information")) return;

    // Check flags for available fields
    if (mb_info->flags & MULTIBOOT1_FLAG_MEM) {
        printf("Memory Information: ");
        printf("  Lower Memory: %u KB, ", mb_info->mem_lower);
        printf("  Upper Memory: %u KB\n", mb_info->mem_upper);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_BOOT_DEVICE) {
        printf("Boot Device: 0x%08X\n", mb_info->boot_device);
    }

    if ((mb_info->flags & MULTIBOOT1_FLAG_CMDLINE) && mb_info->cmdline != 0) {
        const char *cmdline = (const char *)(uintptr_t)mb_info->cmdline;
        printf("Command Line: %s\n", cmdline);
    }

    if ((mb_info->flags & MULTIBOOT1_FLAG_MODS) && mb_info->mods_addr != 0) {
        printf("Modules:\n");
        const multiboot1_module_t *mods =
            (const multiboot1_module_t *)(uintptr_t)mb_info->mods_addr;
        for (uint32_t i = 0; i < mb_info->mods_count; i++) {
            printf("  Module %u:\n", i + 1);
            printf("    Start Address: 0x%x\n", mods[i].mod_start);
            printf("    End Address: 0x%x\n", mods[i].mod_end);
            const char *mod_cmdline = (const char *)(uintptr_t)mods[i].string;
            printf("    Command Line: %s\n", mod_cmdline ? mod_cmdline : "(none)");
        }
    }

    bool valid_mmap = (mb_info->flags & MULTIBOOT1_FLAG_MMAP) &&
                      mb_info->mmap_addr != 0 && mb_info->mmap_length != 0 &&
                      mb_info->mmap_length <= UINTPTR_MAX - mb_info->mmap_addr;
    if (valid_mmap) {
        printf("Memory Map:\n");
        const uint8_t *cursor = (const uint8_t *)(uintptr_t)mb_info->mmap_addr;
        const uint8_t *mmap_end = cursor + mb_info->mmap_length;
        if (!reserve_boot_region(mb_info->mmap_addr, mb_info->mmap_length,
                                 "Multiboot memory-map storage")) return;

        printf("------------------------------------------------------------\n");
        printf("| Address                 | Length        | Type (1=Usable)|\n");
        printf("------------------------------------------------------------\n");

        while ((size_t)(mmap_end - cursor) >= sizeof(uint32_t)) {
            const multiboot1_mmap_entry_t *mmap =
                (const multiboot1_mmap_entry_t*)cursor;
            size_t remaining = (size_t)(mmap_end - cursor);
            if (mmap->size < sizeof(multiboot1_mmap_entry_t) - sizeof(mmap->size) ||
                (size_t)mmap->size > remaining - sizeof(mmap->size)) {
                printf("Fatal: malformed Multiboot memory-map entry.\n");
                memory_map_reset();
                return;
            }
            size_t entry_size = (size_t)mmap->size + sizeof(mmap->size);

            printf("| %016llX ", mmap->base_addr);
            uint64_t region_end = mmap->length == 0 ? mmap->base_addr :
                (mmap->length - 1U > UINT64_MAX - mmap->base_addr
                    ? UINT64_MAX : mmap->base_addr + mmap->length - 1U);
            printf("| %016llX | ", region_end);
            printf("%-13llu | ", mmap->length);
            printf("%-14u |\n", mmap->type);

            if (mmap->type == 1) {
                if (!add_boot_usable_region(mmap->base_addr, mmap->length)) {
                    return;
                }
            } else if (mmap->length != 0 &&
                       memory_reserve_region(mmap->base_addr,
                                             mmap->length) != 0) {
                printf("Fatal: reserved memory-map regions exceed capacity.\n");
                memory_map_reset();
                return;
            }

            cursor += entry_size;
        }
        if (cursor != mmap_end) {
            printf("Fatal: truncated Multiboot memory map.\n");
            memory_map_reset();
            return;
        }
        printf("------------------------------------------------------------\n");
    } else if (mb_info->flags & MULTIBOOT1_FLAG_MEM) {
        /* Multiboot's basic memory fields are a conservative fallback only. */
        if (!add_boot_usable_region(
                0, (uint64_t)mb_info->mem_lower * 1024U) ||
            !add_boot_usable_region(
                0x100000U, (uint64_t)mb_info->mem_upper * 1024U)) return;
    }

    if (total_memory == 0) {
        printf("Fatal: boot memory map contains no usable regions.\n");
        memory_map_reset();
        return;
    }

    if ((mb_info->flags & MULTIBOOT1_FLAG_MODS) && mb_info->mods_addr != 0) {
        if (!reserve_boot_region(
                mb_info->mods_addr,
                (uint64_t)mb_info->mods_count * sizeof(multiboot1_module_t),
                "Multiboot module table")) return;
        const multiboot1_module_t *mods =
            (const multiboot1_module_t *)(uintptr_t)mb_info->mods_addr;
        for (uint32_t i = 0; i < mb_info->mods_count; ++i) {
            if (mods[i].mod_end > mods[i].mod_start) {
                if (!reserve_boot_region(
                        mods[i].mod_start,
                        mods[i].mod_end - mods[i].mod_start,
                        "Multiboot module payload")) return;
            }
        }
    }

    if ((mb_info->flags & MULTIBOOT1_FLAG_BOOTLOADER) &&
        mb_info->boot_loader_name != 0) {
        const char *bootloader_name =
            (const char *)(uintptr_t)mb_info->boot_loader_name;
        printf("Bootloader Name: %s\n", bootloader_name);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_VBE) {
        printf("VBE Information:\n");
        printf("Control Info: 0x%08X ", mb_info->vbe_control_info);
        printf("Mode Info: 0x%08X ", mb_info->vbe_mode_info);
        printf("Mode: 0x%04X\n", mb_info->vbe_mode);
        printf("Interface Segment: 0x%04X ", mb_info->vbe_interface_seg);
        printf("Offset: 0x%04X ", mb_info->vbe_interface_off);
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
