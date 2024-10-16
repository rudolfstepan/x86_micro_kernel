#include "system.h"
#include "io.h"
#include "keyboard.h"

// custom implementation
#include "../toolchain/stdlib.h"
#include "../toolchain/stdio.h"

MultibootInfo* sys_mb_info;


// Check if a character is printable
int is_printable(char ch) {
    return (ch >= 32 && ch < 127);
}

// Convert a byte to a printable character or '.'
char to_printable_char(char ch) {
    return is_printable(ch) ? ch : '.';
}

// Memory dump function
void memory_dump(uint32_t start_address, uint32_t end_address) {
    if (end_address == 0) {
        end_address = start_address + (BYTES_PER_LINE * MAX_LINES); // Default length for 20 lines
    }

    uint8_t* ptr = (uint8_t*)start_address;
    int line_count = 0;

    while (ptr < (uint8_t*)end_address) {
        printf("%08X: ", (unsigned int)(uintptr_t)ptr);

        // Print each byte in hex and store ASCII characters
        char ascii[BYTES_PER_LINE + 1];

        for (int i = 0; i < BYTES_PER_LINE; ++i) {
            if (ptr + i < (uint8_t*)end_address) {
                printf("%02X ", ptr[i]);
                ascii[i] = is_printable(ptr[i]) ? ptr[i] : '.';
            } else {
                printf("   ");
                ascii[i] = ' ';
            }
        }

        printf(" |%s|\n", ascii);
        ptr += BYTES_PER_LINE;
        line_count++;

        if (line_count >= MAX_LINES) {
            wait_for_enter(); // Wait for user to press Enter
            line_count = 0;   // Reset line count
        }
    }
}

// Print the memory map
void print_memory_map(const MultibootInfo* mb_info) {
    int line_count = 0;
    if (mb_info->flags & 1 << 6) {
        MemoryMapEntry* mmap = (MemoryMapEntry*)mb_info->mmap_addr;
        while ((unsigned int)mmap < mb_info->mmap_addr + mb_info->mmap_length) {
            printf("Memory Base: 0x%llx, Length: 0x%llx, Type: %u\n", 
                   mmap->base_addr, mmap->length, mmap->type);
            mmap = (MemoryMapEntry*)((unsigned int)mmap + mmap->size + sizeof(mmap->size));

            if (++line_count == 20) {
                printf("Press Enter to continue...\n");
                wait_for_enter();
                line_count = 0;
            }
        }
    } else {
        printf("Memory map not available.\n");
    }
}

