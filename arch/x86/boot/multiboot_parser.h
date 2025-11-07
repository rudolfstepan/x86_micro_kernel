/**
 * @file multiboot.h
 * @brief Multiboot parsing function declarations
 */

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>
#include "arch/x86/mbheader.h"

// Multiboot 1 parsing
void parse_multiboot1_info(const multiboot1_info_t *mb_info);

// Multiboot 2 parsing
void parse_multiboot2_info(const multiboot2_info_t *mb_info);
void print_efi_memory_map(const multiboot2_info_t *mb_info);
uint64_t compute_total_memory(const multiboot2_info_t *mb_info);

#endif // MULTIBOOT_H
