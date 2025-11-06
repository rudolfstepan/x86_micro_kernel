#include "kernel/init/prg.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"


program_header_t myHeader = {
    .identifier = "",  // Placeholder, to be filled by the linker
    .magic_number = 0,     // Placeholder, to be filled by the linker
    .entry_point = 0,      // Placeholder, to be filled by the linker
    .program_size = 0,     // Placeholder, to be filled by the linker
    .base_address = 0,     // Placeholder, to be filled by the linker
    .relocation_offset = 0,// Placeholder, to be filled by the linker
    .relocation_size = 0   // Placeholder, to be filled by the linker
};

/* Function to load program into memory and relocate it */
void load_and_relocate_program(void* program_src, void* target_address) {
    // program_header_t* header = (program_header_t*)program_src;

    // // Step 1: Copy the entire program (header + program data) to the target address
    // size_t total_size = header->program_size + sizeof(program_header_t);
    // memcpy(target_address, program_src, total_size);

    // printf("Total size: %d\n", total_size);

    // // Step 2: Apply relocation, using the target address as the base
    // uint32_t relocation_offset = (uint32_t)target_address + header->relocation_offset;
    // uint32_t relocation_count = header->relocation_size / sizeof(uint32_t);
    // uint32_t address_offset = (uint32_t)target_address - (uint32_t)program_src;

    // apply_relocation((uint32_t*)relocation_offset, relocation_count, address_offset);

    // // Step 3: Calculate the absolute entry point address from the base address
    // void (*entry)() = (void (*)())((uint32_t)target_address + header->entry_point);
    // entry();

    // printf("Program loaded and executed successfully at %p\n", entry);

    //hex_dump((unsigned char*)target_address, total_size);
}

/* Apply relocations based on the offset between the program's compiled address and loaded address */
void apply_relocation(uint32_t* relocation_table, uint32_t relocation_count, uint32_t offset) {
    for (uint32_t i = 0; i < relocation_count; i++) {
        uint32_t* address = (uint32_t*)((uint8_t*)relocation_table[i]);
        
        // Adjust the address by the offset to account for the new base address
        *address += offset;
    }
}

// Function to load an ELF binary from memory
int load_elf(void *elf_data) {
    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_data;

    // Check ELF magic number (first 4 bytes)
    if (memcmp(ehdr->e_ident, "\x7F""ELF", 4) != 0) {
        printf("Not a valid ELF file.\n");
        return -1;
    }

    // Parse and load each program header
    Elf32_Phdr *phdr = (Elf32_Phdr *)((uint8_t *)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) { // Only load PT_LOAD segments
            // Calculate the memory address for this segment using its virtual address
            void* segment_address = (void*)phdr[i].p_vaddr;

            // Copy the segment data from the ELF file into the correct memory location
            memcpy(segment_address, (uint8_t *)elf_data + phdr[i].p_offset, phdr[i].p_filesz);

            // Zero any remaining memory if p_memsz > p_filesz
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((void *)((uint8_t*)segment_address + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
            }

            printf("Loaded segment at virtual address: 0x%x, size: 0x%x\n", (uint32_t)segment_address, phdr[i].p_memsz);
        }
    }

    // Entry point is an absolute virtual address in the ELF header
    void (*entry_point)() = (void (*)())ehdr->e_entry;

    printf("Jumping to entry point at 0x%x\n", (uint32_t)entry_point);
    //entry_point();

    hex_dump((unsigned char*)entry_point, 512);

    return 0;
}