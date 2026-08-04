#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define MAX_PROGRAMS 256 // Maximum number of running programs
#define MAX_USER_ALLOCATIONS 16

typedef struct {
    uint32_t address;
    uint32_t requested_size;
    uint32_t mapped_size;
    bool allocated;
} user_allocation_t;

typedef struct {
    int pid;
    int task_id;
    char name[32];
    bool is_running;
    bool uses_shared_program_image;
    uint32_t heap_next;
    user_allocation_t user_allocations[MAX_USER_ALLOCATIONS];
    // Add more fields as needed, e.g., priority, state, etc.
} Process;


int create_process(void* entry_point);
int create_process_for_file(const char *filename);
void wait_for_process(int pid);
void* process_user_malloc(size_t size);
int process_user_free(void* pointer);
void* process_user_realloc(void* pointer, size_t size);
void list_running_processes(void);
void terminate_process(int pid);

void start_program_execution(long entry_point);
void load_and_execute_program(const char* program_name);
int load_program_into_memory(const char* program_name, uint32_t address);

#endif // PROCESS_H
