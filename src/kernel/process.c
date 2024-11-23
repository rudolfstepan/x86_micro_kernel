#include "process.h"
#include <stdbool.h>

#include "toolchain/strings.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "filesystem/fat32/fat32.h"
#include "prg.h"

#define PROGRAM_LOAD_ADDRESS 0x01100000 // default address where the program will be loaded into memory except in the case of a program header


Process process_list[MAX_PROGRAMS];
int next_pid = 1; // PID counter starting at 1

// execute the program at the specified entry point
void start_program_execution(long entryPoint) {
    void (*program)() = (void (*)())entryPoint;
    program(); // Jump to the program
}

// load the program into memory
void load_and_execute_program(const char* programName) {
    // Load the program into the specified memory location
    if (fat32_load_file(programName, (void*)PROGRAM_LOAD_ADDRESS) > 0) {
        program_header_t* header = (program_header_t*)PROGRAM_LOAD_ADDRESS;

        // print the program header details
        // printf("Program Header Details:\n");
        // printf("Identifier: %s\n", header->identifier);
        // printf("Magic Number: %u\n", header->magic_number);
        // printf("Program size: %d\n", header->program_size);
        // printf("Entry point: %p\n", header->entry_point);
        // printf("Base address: %p\n", header->base_address);
        // printf("Relocation offset: %d\n", header->relocation_offset);
        // printf("Relocation size: %d\n", header->relocation_size);
        // printf("Program address: %p\n", (void*)PROGRAM_LOAD_ADDRESS);

        // get the address of the userspace
        uint32_t* relocation_table = (uint32_t*)(PROGRAM_LOAD_ADDRESS + header->relocation_offset);
        uint32_t relocation_count = header->relocation_size / sizeof(uint32_t);

        // apply the relocation
        apply_relocation(relocation_table, relocation_count, PROGRAM_LOAD_ADDRESS);

        printf("Start prg at address: %p\n", header->entry_point + PROGRAM_LOAD_ADDRESS);

        // execute the program
        void (*program)() = (void (*)())(header->entry_point + PROGRAM_LOAD_ADDRESS);
        program();

        //start_program_execution(header->entry_point);

        //load_elf((void*)PROGRAM_LOAD_ADDRESS);


    } else {
        printf("%s not found\n", programName);
    }
}

void load_program_into_memory(const char* programName, uint32_t address) {
    // Load the program into the specified memory location
    if (fat32_load_file(programName, (void*)address) > 0) {
        program_header_t* header = (program_header_t*)address;
        printf("entryPoint: %p\n", address + header->entry_point);
    } else {
        printf("%s not found\n", programName);
    }
}

int create_process(const char *program_name) {
    // Find an available slot in the process list
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (!process_list[i].is_running) {
            process_list[i].pid = next_pid++;
            strcpy(process_list[i].name, program_name);
            process_list[i].is_running = true;

            // Load and execute the program here
            load_and_execute_program(program_name);

            printf(">>>Program '%s' started with PID %d\n", program_name, process_list[i].pid);
            return process_list[i].pid;
        }
    }

    // No available slots
    printf("Error: Maximum number of running programs reached.\n");
    return -1;
}

void list_running_processes() {
    printf("Running programs:\n");
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running) {
            printf("PID %d: %s\n", process_list[i].pid, process_list[i].name);
        }
    }
}

void terminate_process(int pid) {
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running && process_list[i].pid == pid) {
            process_list[i].is_running = false;
            printf("Program '%s' with PID %d terminated.\n", process_list[i].name, pid);
            return;
        }
    }

    printf("Error: PID %d not found.\n", pid);
}
