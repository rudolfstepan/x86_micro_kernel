#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define MAX_PROGRAMS 256 // Maximum number of running programs
#define MAX_USER_ALLOCATIONS 16
#define MAX_PROCESS_FILES 8
#define PROCESS_FD_BASE 3
#define PROCESS_PATH_MAX 256

struct vfs_node;

typedef struct {
    uint32_t address;
    uint32_t requested_size;
    uint32_t mapped_size;
    bool allocated;
} user_allocation_t;

typedef struct {
    struct vfs_node *node;
    uint32_t offset;
    bool in_use;
    bool writable;
} process_file_t;

typedef struct {
    int pid;
    int parent_pid;
    int task_id;
    int exit_status;
    char name[32];
    bool is_running;
    bool has_exited;
    bool uses_shared_program_image;
    uint32_t heap_next;
    user_allocation_t user_allocations[MAX_USER_ALLOCATIONS];
    process_file_t files[MAX_PROCESS_FILES];
    char working_directory[PROCESS_PATH_MAX];
    // Add more fields as needed, e.g., priority, state, etc.
} Process;


int create_process(void* entry_point);
int create_process_for_file(const char *filename);
int create_process_for_file_args(const char* filename, int argc,
                                 const char* const* argv,
                                 const char* working_directory);
void wait_for_process(int pid);
int process_spawn(Process* parent, const char* path);
int process_wait_status(Process* parent, int pid, int* status);
void process_orphan_children(int parent_pid);
void* process_user_malloc(size_t size);
int process_user_free(void* pointer);
void* process_user_realloc(void* pointer, size_t size);
int process_file_open(Process* process, const char* path);
int process_file_read(Process* process, int descriptor, void* buffer,
                      size_t size);
int process_file_create(Process* process, const char* path);
int process_file_write(Process* process, int descriptor, const void* buffer,
                       size_t size);
int process_file_unlink(Process* process, const char* path);
int process_file_close(Process* process, int descriptor);
void process_close_all_files(Process* process);
int process_resolve_path(const Process* process, const char* path,
                         char resolved[PROCESS_PATH_MAX]);
void list_running_processes(void);
void terminate_process(int pid);

void start_program_execution(long entry_point);
void load_and_execute_program(const char* program_name);
int load_program_into_memory(const char* program_name, uint32_t address);

#endif // PROCESS_H
