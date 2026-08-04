#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>


#define MAX_PROGRAMS 256 // Maximum number of running programs

typedef struct {
    int pid;
    int task_id;
    char name[32];
    bool is_running;
    bool uses_shared_program_image;
    // Add more fields as needed, e.g., priority, state, etc.
} Process;


int create_process(void* entry_point);
int create_process_for_file(const char *filename);
void wait_for_process(int pid);
void list_running_processes(void);
void terminate_process(int pid);

void start_program_execution(long entry_point);
void load_and_execute_program(const char* program_name);
int load_program_into_memory(const char* program_name, uint32_t address);

#endif // PROCESS_H
