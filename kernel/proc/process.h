/**
 * @file kernel/proc/process.h
 * @brief Öffentlicher Prozess-Lifecycle-Vertrag.
 *
 * Layer: Ring-0 process subsystem.
 * Contract: Zustand und Generation werden vor Ressourcenänderungen geprüft.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "include/kernel/ipc.h"
#include "kernel/sched/wait_queue.h"


#define MAX_PROGRAMS 256 // Maximum number of running programs
#define MAX_USER_ALLOCATIONS 16
#define MAX_PROCESS_FILES 8
#define PROCESS_FD_BASE 3
#define PROCESS_PATH_MAX 256
#define SUPERVISED_PROCESS_RESERVE 1U
#define SUPERVISED_RESTART_FRAME_RESERVE 32U
#define PROCESS_DOMAIN_PROFILE_VERSION 1U
#define PROCESS_DOMAIN_SYSCALL_WORDS 4U
#define PROCESS_DOMAIN_SYSCALL_LIMIT 111U

typedef enum {
    PROCESS_DOMAIN_COMPATIBILITY = 1,
    PROCESS_DOMAIN_PROBE = 2,
    PROCESS_DOMAIN_STORAGE = 3,
    PROCESS_DOMAIN_ADMIN = 4,
    PROCESS_DOMAIN_COMPONENT_ADMIN = 5,
} process_domain_kind_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t kind;
    uint32_t allowed_syscalls[PROCESS_DOMAIN_SYSCALL_WORDS];
} process_domain_profile_t;

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
    uint32_t resource_handle;
    uint8_t kind;
    bool in_use;
    bool writable;
} process_file_t;

enum {
    PROCESS_DESCRIPTOR_FILE = 1U,
    PROCESS_DESCRIPTOR_UDP_SOCKET = 2U,
    PROCESS_DESCRIPTOR_TCP_SOCKET = 3U,
};

typedef struct Process {
    int pid;
    uint32_t generation;
    int parent_pid;
    uint32_t parent_generation;
    int task_id;
    int exit_status;
    char name[32];
    bool is_running;
    bool has_exited;
    bool uses_shared_program_image;
    uint32_t heap_next;
    user_allocation_t user_allocations[MAX_USER_ALLOCATIONS];
    process_file_t files[MAX_PROCESS_FILES];
    process_domain_profile_t domain_profile;
    ipc_capability_t ipc_capabilities[IPC_MAX_CAPABILITIES_PER_PROCESS];
    char working_directory[PROCESS_PATH_MAX];
    wait_queue_t exit_waiters;
    // Add more fields as needed, e.g., priority, state, etc.
} Process;

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    int32_t state;
    int32_t exit_status;
    char name[32];
} process_info_t;

#define PROCESS_STATE_READY 0
#define PROCESS_STATE_RUNNING 1
#define PROCESS_STATE_SLEEPING 2
#define PROCESS_STATE_WAITING 3
#define PROCESS_STATE_ZOMBIE 4


int create_process(void* entry_point);
int create_process_for_file(const char *filename);
int create_process_for_file_args(const char* filename, int argc,
                                 const char* const* argv,
                                 const char* working_directory);
bool process_cache_rescue_programs(void);
void wait_for_process(int pid);
int process_spawn(Process* parent, const char* path);
int process_spawn_args(Process* parent, const char* path, int argc,
                       const char* const* argv);
int process_spawn_supervised(const char *path, int argc,
                             const char *const *argv,
                             process_domain_kind_t domain_kind);
bool process_syscall_allowed(const Process *process, uint32_t syscall_index);
int process_terminate_authorized(Process *requester, int pid);
int process_get_identity(int pid, uint32_t *generation_out);
bool process_identity_alive(int pid, uint32_t generation);
int process_ipc_delegate(Process *source, ipc_handle_t handle,
                         int target_pid, uint32_t rights);
int process_ipc_delegate_identity(int source_pid, uint32_t source_generation,
                                  ipc_handle_t handle, Process *target,
                                  uint32_t rights);
int process_wait_status(Process* parent, int pid, int* status);
int process_wait_status_locked(Process* parent, int pid, int* status,
                               wait_queue_t** wait_queue);
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
int process_file_sync(Process* process, int descriptor);
int process_file_unlink(Process* process, const char* path);
int process_file_close(Process* process, int descriptor);
int process_descriptor_install(Process *process, uint8_t kind,
                               uint32_t resource_handle);
int process_descriptor_resolve(const Process *process, int descriptor,
                               uint8_t kind, uint32_t *resource_handle_out);
int process_descriptor_release(Process *process, int descriptor, uint8_t kind);
void process_close_all_files(Process* process);
int process_revoke_files_for_resource(uint32_t resource,
                                      uint32_t* revoked_out);
int process_resolve_path(const Process* process, const char* path,
                         char resolved[PROCESS_PATH_MAX]);
int process_get_working_directory(const Process* process, char* buffer,
                                  size_t size);
int process_set_working_directory(Process* process, const char* path);
int process_get_info(uint32_t index, process_info_t* info);
int process_terminate(int pid);

#endif // PROCESS_H
