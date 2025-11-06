#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>


#define MAX_PROGRAMS 256 // Maximum number of running programs

typedef struct {
    int pid;
    char name[32];
    bool is_running;
    // Add more fields as needed, e.g., priority, state, etc.
} Process;


int create_process(void* entry_point);
int create_process_for_file(const char *filename);
void list_running_processes();
void terminate_process(int pid);

void start_program_execution(long entryPoint);
void load_and_execute_program(const char* programName);
void load_program_into_memory(const char* programName, uint32_t address);

#endif // PROCESS_H