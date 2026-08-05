#include "kernel/proc/process.h"
#include "kernel/proc/program_image.h"
#include <stdbool.h>

#include "arch/x86/include/interrupt.h"
#include "arch/x86/mm/paging.h"
#include "mm/kmalloc.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "fs/vfs/vfs.h"
#include "kernel/init/prg.h"
#include "kernel/sched/scheduler.h"

#define PROGRAM_STAGING_ADDRESS KERNEL_PROGRAM_REGION_START
#define USER_PROGRAM_ADDRESS USER_BASE
#define USER_STACK_TOP (USER_TOP - PAGE_SIZE)
#define USER_STACK_SIZE (8U * PAGE_SIZE)
#define PROGRAM_REGION_SIZE  KERNEL_PROGRAM_REGION_SIZE
static int load_program_file(const char *program_name, uint32_t address) {
    if (!memory_region_is_usable(address, PROGRAM_REGION_SIZE)) {
        printf("Program image region is not backed by usable RAM.\n");
        return -1;
    }

    vfs_node_t* node = NULL;
    int result = vfs_open(program_name, &node);
    if (result != VFS_OK || !node) {
        return -1;
    }
    if (node->type != VFS_FILE || node->size < sizeof(program_header_t) ||
        node->size > PROGRAM_REGION_SIZE) {
        (void)vfs_close(node);
        return -1;
    }

    uint32_t loaded_size = node->size;
    /* Read the image sequentially in one VFS operation.  Restarting a FAT
     * read every page would walk the cluster chain from its beginning for
     * each chunk and makes larger programs progressively slower to start. */
    result = vfs_read(node, 0, loaded_size,
                      (uint8_t*)(uintptr_t)address);
    if (result != (int)loaded_size) {
        (void)vfs_close(node);
        return -1;
    }
    if (vfs_close(node) != VFS_OK ||
        program_image_validate((const void*)(uintptr_t)address, loaded_size,
                               PROGRAM_REGION_SIZE) != 0) {
        return -1;
    }
    return (int)loaded_size;
}


Process process_list[MAX_PROGRAMS];
int next_pid = 1; // PID counter starting at 1

static int allocate_pid_locked(void) {
    for (int attempt = 0; attempt <= MAX_PROGRAMS; ++attempt) {
        int candidate = next_pid;
        next_pid = next_pid == INT_MAX ? 1 : next_pid + 1;

        bool in_use = false;
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if (process_list[i].is_running && process_list[i].pid == candidate) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            return candidate;
        }
    }
    return -1;
}

static void release_process_slot(Process *process) {
    process_close_all_files(process);
    uint32_t flags = irq_save();
    process->is_running = false;
    process->uses_shared_program_image = false;
    process->task_id = -1;
    irq_restore(flags);
}

/* Reserve list state atomically, but do slow file/heap work with IRQs enabled. */
static int claim_process_slot(const char *name, bool shared_image) {
    uint32_t flags = irq_save();

    if (shared_image) {
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if (process_list[i].is_running &&
                process_list[i].uses_shared_program_image) {
                irq_restore(flags);
                return -2;
            }
        }
    }

    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (!process_list[i].is_running &&
            !(process_list[i].has_exited && process_list[i].parent_pid > 0)) {
            Process *process = &process_list[i];
            int pid = allocate_pid_locked();
            if (pid < 0) {
                irq_restore(flags);
                return -1;
            }
            process->pid = pid;
            process->parent_pid = 0;
            process->task_id = -1;
            process->exit_status = 0;
            process->has_exited = false;
            process->uses_shared_program_image = shared_image;
            process->heap_next = USER_HEAP_BASE;
            memset(process->user_allocations, 0,
                   sizeof(process->user_allocations));
            memset(process->files, 0, sizeof(process->files));
            strcpy(process->working_directory, "/");
            strncpy(process->name, name, sizeof(process->name) - 1U);
            process->name[sizeof(process->name) - 1U] = '\0';
            process->is_running = true;
            irq_restore(flags);
            return i;
        }
    }

    irq_restore(flags);
    return -1;
}

// execute the program at the specified entry point
void start_program_execution(long entry_point) {
    void (*program)() = (void (*)())entry_point;
    program(); // Jump to the program
}

// load the program into memory
void load_and_execute_program(const char* program_name) {
    if (create_process_for_file(program_name) < 0) {
        printf("Unable to load valid program '%s'\n", program_name);
    }
}

int load_program_into_memory(const char* program_name, uint32_t address) {
    // Load the program into the specified memory location
    int loaded_size = load_program_file(program_name, address);
    if (loaded_size > 0) {
        return loaded_size;
    } else {
        printf("Unable to load valid program '%s'\n", program_name);
        return -1;
    }
}

int create_process_for_file(const char *filename) {
    const char *arguments[] = {filename};
    return create_process_for_file_args(filename, 1, arguments, "/");
}

static int build_user_arguments(page_directory_t *page_directory, int argc,
                                const char *const *argv,
                                uint32_t *user_stack) {
    if (argc < 0 || argc > 32 || (argc != 0 && argv == NULL)) return -1;
    uint32_t addresses[32];
    uint32_t stack = USER_STACK_TOP;

    for (int i = argc - 1; i >= 0; --i) {
        if (argv[i] == NULL) return -1;
        size_t length = strlen(argv[i]) + 1U;
        if (length > 256U || stack < USER_STACK_TOP - USER_STACK_SIZE + length) {
            return -1;
        }
        stack -= (uint32_t)length;
        if (copy_to_user_space(page_directory, stack, argv[i], length) != 0) {
            return -1;
        }
        addresses[i] = stack;
    }

    stack &= ~3U;
    uint32_t zero = 0;
    stack -= sizeof(uint32_t);
    if (copy_to_user_space(page_directory, stack, &zero, sizeof(zero)) != 0) {
        return -1;
    }
    for (int i = argc - 1; i >= 0; --i) {
        stack -= sizeof(uint32_t);
        if (copy_to_user_space(page_directory, stack, &addresses[i],
                               sizeof(addresses[i])) != 0) return -1;
    }
    uint32_t user_argv = stack;
    stack -= sizeof(uint32_t);
    if (copy_to_user_space(page_directory, stack, &user_argv,
                           sizeof(user_argv)) != 0) return -1;
    stack -= sizeof(uint32_t);
    uint32_t user_argc = (uint32_t)argc;
    if (copy_to_user_space(page_directory, stack, &user_argc,
                           sizeof(user_argc)) != 0) return -1;
    stack -= sizeof(uint32_t);
    if (copy_to_user_space(page_directory, stack, &zero, sizeof(zero)) != 0) {
        return -1;
    }
    *user_stack = stack;
    return 0;
}

int create_process_for_file_args(const char *filename, int argc,
                                 const char *const *argv,
                                 const char *working_directory) {
    if (filename == NULL || *filename == '\0') {
        return -1;
    }

    const char *display_name = filename;
    for (const char *cursor = filename; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') display_name = cursor + 1;
    }
    int slot = claim_process_slot(display_name, false);
    if (slot < 0) {
        printf("Error: Maximum number of running programs reached.\n");
        return -1;
    }

    Process *process = &process_list[slot];
    if (working_directory == NULL || working_directory[0] != '/' ||
        strlen(working_directory) >= sizeof(process->working_directory)) {
        release_process_slot(process);
        return -1;
    }
    strcpy(process->working_directory, working_directory);
    int pid = process->pid;
    int loaded_size = load_program_into_memory(filename, PROGRAM_STAGING_ADDRESS);
    if (loaded_size < 0) {
        release_process_slot(process);
        return -1;
    }

    program_header_t* header = (program_header_t*)PROGRAM_STAGING_ADDRESS;
    uint32_t memory_image_size = (uint32_t)sizeof(*header) +
                                 header->program_size;
    uint32_t stored_image_size = header->relocation_offset;
    if (header->relocation_size == 0 &&
        header->base_address != USER_PROGRAM_ADDRESS) {
        printf("Program '%s' was linked for the wrong load address.\n",
               filename);
        release_process_slot(process);
        return -1;
    }
    uint32_t *relocation_table = (uint32_t*)(
        PROGRAM_STAGING_ADDRESS + header->relocation_offset);
    if (apply_relocation(relocation_table,
                         header->relocation_size / sizeof(uint32_t),
                         header->base_address, USER_PROGRAM_ADDRESS,
                         memory_image_size) != 0) {
        printf("Invalid relocation table in '%s'.\n", filename);
        release_process_slot(process);
        return -1;
    }

    page_directory_t *page_directory = create_page_directory();
    if (page_directory == NULL) {
        release_process_slot(process);
        return -1;
    }

    uint32_t mapped_size = (memory_image_size + PAGE_SIZE - 1U) &
                           ~(PAGE_SIZE - 1U);
    for (uint32_t offset = 0; offset < mapped_size; offset += PAGE_SIZE) {
        uint32_t frame = (uint32_t)allocate_frame();
        if (frame == 0 || map_page(page_directory,
                                  USER_PROGRAM_ADDRESS + offset, frame,
                                  PAGE_USER | PAGE_RW) != 0) {
            if (frame != 0) free_frame(frame);
            free_page_directory(page_directory);
            release_process_slot(process);
            return -1;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
        uint32_t amount = 0;
        if (offset < stored_image_size) {
            amount = stored_image_size - offset;
            if (amount > PAGE_SIZE) amount = PAGE_SIZE;
            memcpy((void*)(uintptr_t)frame,
                   (const void*)(uintptr_t)(PROGRAM_STAGING_ADDRESS + offset),
                   amount);
        }
    }
    for (uint32_t address = USER_STACK_TOP - USER_STACK_SIZE;
         address < USER_STACK_TOP; address += PAGE_SIZE) {
        uint32_t frame = (uint32_t)allocate_frame();
        if (frame == 0 || map_page(page_directory, address, frame,
                                  PAGE_USER | PAGE_RW) != 0) {
            if (frame != 0) free_frame(frame);
            free_page_directory(page_directory);
            release_process_slot(process);
            return -1;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
    }

    uint32_t* kernel_stack = (uint32_t*)k_malloc(STACK_SIZE);
    if (kernel_stack == NULL) {
        free_page_directory(page_directory);
        release_process_slot(process);
        return -1;
    }

    uint32_t user_stack;
    if (build_user_arguments(page_directory, argc, argv, &user_stack) != 0) {
        k_free(kernel_stack);
        free_page_directory(page_directory);
        release_process_slot(process);
        return -1;
    }

    int task_id = create_user_task(
        header->entry_point + USER_PROGRAM_ADDRESS, user_stack,
        kernel_stack, page_directory, process);
    if (task_id < 0) {
        k_free(kernel_stack);
        free_page_directory(page_directory);
        release_process_slot(process);
        return -1;
    }
    return pid;
}

int create_process(void* entry_point) {
    if (entry_point == NULL) {
        return -1;
    }

    int slot = claim_process_slot("Unknown", false);
    if (slot < 0) {
        printf("Error: Maximum number of running programs reached.\n");
        return -1;
    }
    Process *process = &process_list[slot];
    int pid = process->pid;

    uint32_t* stack = (uint32_t*)k_malloc(STACK_SIZE);
    if (stack == NULL) {
        printf("Error: Failed to allocate stack for process\n");
        release_process_slot(process);
        return -1;
    }

    int task_id = create_task((void (*)())entry_point, stack, process);
    if (task_id < 0) {
        k_free(stack);
        release_process_slot(process);
        return -1;
    }
    return pid;
}

static int map_user_allocation(user_allocation_t *allocation) {
    uint32_t mapped = 0;
    while (mapped < allocation->mapped_size) {
        uint32_t frame = (uint32_t)allocate_frame();
        if (frame == 0 ||
            map_page(paging_current_directory(), allocation->address + mapped,
                     frame, PAGE_USER | PAGE_RW) != 0) {
            if (frame != 0) free_frame(frame);
            while (mapped != 0) {
                mapped -= PAGE_SIZE;
                (void)unmap_page(paging_current_directory(),
                                 allocation->address + mapped, true);
            }
            return -1;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
        mapped += PAGE_SIZE;
    }
    return 0;
}

void *process_user_malloc(size_t size) {
    Process *process = scheduler_current_process();
    if (process == NULL || size == 0 || size > UINT32_MAX - PAGE_SIZE) {
        return NULL;
    }
    uint32_t mapped_size = ((uint32_t)size + PAGE_SIZE - 1U) &
                           ~(PAGE_SIZE - 1U);
    uint32_t flags = irq_save();
    user_allocation_t *slot = NULL;

    for (int i = 0; i < MAX_USER_ALLOCATIONS; ++i) {
        user_allocation_t *candidate = &process->user_allocations[i];
        if (!candidate->allocated && candidate->address != 0 &&
            candidate->mapped_size >= mapped_size) {
            slot = candidate;
            break;
        }
        if (!candidate->allocated && candidate->address == 0 && slot == NULL) {
            slot = candidate;
        }
    }
    if (slot == NULL) {
        irq_restore(flags);
        return NULL;
    }

    if (slot->address == 0) {
        if (process->heap_next < USER_HEAP_BASE ||
            process->heap_next >= USER_HEAP_TOP ||
            mapped_size > USER_HEAP_TOP - process->heap_next) {
            irq_restore(flags);
            return NULL;
        }
        slot->address = process->heap_next;
        slot->mapped_size = mapped_size;
        process->heap_next += mapped_size;
    }
    if (map_user_allocation(slot) != 0) {
        irq_restore(flags);
        return NULL;
    }
    slot->requested_size = (uint32_t)size;
    slot->allocated = true;
    void *result = (void*)(uintptr_t)slot->address;
    irq_restore(flags);
    return result;
}

int process_user_free(void *pointer) {
    if (pointer == NULL) return 0;
    Process *process = scheduler_current_process();
    if (process == NULL) return -1;

    uint32_t address = (uint32_t)(uintptr_t)pointer;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_USER_ALLOCATIONS; ++i) {
        user_allocation_t *allocation = &process->user_allocations[i];
        if (allocation->allocated && allocation->address == address) {
            for (uint32_t offset = 0; offset < allocation->mapped_size;
                 offset += PAGE_SIZE) {
                (void)unmap_page(paging_current_directory(), address + offset,
                                 true);
            }
            allocation->requested_size = 0;
            allocation->allocated = false;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

void *process_user_realloc(void *pointer, size_t size) {
    if (pointer == NULL) return process_user_malloc(size);
    if (size == 0) {
        (void)process_user_free(pointer);
        return NULL;
    }
    Process *process = scheduler_current_process();
    if (process == NULL) return NULL;

    user_allocation_t *old = NULL;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_USER_ALLOCATIONS; ++i) {
        if (process->user_allocations[i].allocated &&
            process->user_allocations[i].address ==
                (uint32_t)(uintptr_t)pointer) {
            old = &process->user_allocations[i];
            break;
        }
    }
    if (old == NULL) {
        irq_restore(flags);
        return NULL;
    }
    uint32_t old_size = old->requested_size;
    if (size <= old->mapped_size) {
        old->requested_size = (uint32_t)size;
        irq_restore(flags);
        return pointer;
    }
    irq_restore(flags);

    void *replacement = process_user_malloc(size);
    if (replacement == NULL) return NULL;
    size_t copy_size = old_size < size ? old_size : size;
    memcpy(replacement, pointer, copy_size);
    (void)process_user_free(pointer);
    return replacement;
}

static int append_path_components(char *output, size_t capacity,
                                  const char *path) {
    size_t output_length = strlen(output);
    const char *cursor = path;
    while (*cursor != '\0') {
        while (*cursor == '/') ++cursor;
        if (*cursor == '\0') break;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/') ++cursor;
        size_t length = (size_t)(cursor - start);
        if (length == 1 && start[0] == '.') continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (output_length > 1) {
                while (output_length > 1 && output[output_length - 1] != '/') {
                    --output_length;
                }
                if (output_length > 1) --output_length;
                output[output_length] = '\0';
            }
            continue;
        }
        if (output_length > 1) {
            if (output_length + 1 >= capacity) return -1;
            output[output_length++] = '/';
        }
        if (length == 0 || length >= capacity - output_length) return -1;
        memcpy(output + output_length, start, length);
        output_length += length;
        output[output_length] = '\0';
    }
    return 0;
}

int process_resolve_path(const Process *process, const char *path,
                         char resolved[PROCESS_PATH_MAX]) {
    if (process == NULL || path == NULL || path[0] == '\0') return -1;
    strcpy(resolved, "/");
    if (path[0] != '/' &&
        append_path_components(resolved, PROCESS_PATH_MAX,
                               process->working_directory) != 0) return -1;
    return append_path_components(resolved, PROCESS_PATH_MAX, path);
}

int process_get_working_directory(const Process *process, char *buffer,
                                  size_t size) {
    if (process == NULL || buffer == NULL) return -1;
    size_t length = strlen(process->working_directory) + 1U;
    if (size < length) return -1;
    memcpy(buffer, process->working_directory, length);
    return 0;
}

int process_set_working_directory(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;

    vfs_dir_entry_t entry;
    if (vfs_stat(resolved, &entry) != VFS_OK || entry.type != VFS_DIRECTORY) {
        return -1;
    }
    strcpy(process->working_directory, resolved);
    return 0;
}

int process_file_open(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_PROCESS_FILES; ++i) {
        if (!process->files[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    vfs_node_t *node = NULL;
    if (vfs_open(resolved, &node) != VFS_OK || node == NULL) return -1;
    if (node->type != VFS_FILE) {
        (void)vfs_close(node);
        return -1;
    }

    process->files[slot].node = node;
    process->files[slot].offset = 0;
    process->files[slot].in_use = true;
    process->files[slot].writable = false;
    return slot + PROCESS_FD_BASE;
}

int process_file_read(Process *process, int descriptor, void *buffer,
                      size_t size) {
    int slot = descriptor - PROCESS_FD_BASE;
    if (process == NULL || buffer == NULL || slot < 0 ||
        slot >= MAX_PROCESS_FILES || !process->files[slot].in_use ||
        size > UINT32_MAX) {
        return -1;
    }
    if (size == 0) return 0;

    process_file_t *file = &process->files[slot];
    int result = vfs_read(file->node, file->offset, (uint32_t)size, buffer);
    if (result > 0) file->offset += (uint32_t)result;
    return result;
}

int process_file_create(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_PROCESS_FILES; ++i) {
        if (!process->files[i].in_use) { slot = i; break; }
    }
    if (slot < 0 || vfs_create(resolved) != VFS_OK) return -1;

    vfs_node_t *node = NULL;
    if (vfs_open(resolved, &node) != VFS_OK || node == NULL ||
        node->type != VFS_FILE) {
        if (node != NULL) (void)vfs_close(node);
        (void)vfs_delete(resolved);
        return -1;
    }
    process->files[slot].node = node;
    process->files[slot].offset = 0;
    process->files[slot].in_use = true;
    process->files[slot].writable = true;
    return slot + PROCESS_FD_BASE;
}

int process_file_write(Process *process, int descriptor, const void *buffer,
                       size_t size) {
    int slot = descriptor - PROCESS_FD_BASE;
    if (process == NULL || buffer == NULL || slot < 0 ||
        slot >= MAX_PROCESS_FILES || !process->files[slot].in_use ||
        !process->files[slot].writable || size > UINT32_MAX) return -1;
    if (size == 0) return 0;
    process_file_t *file = &process->files[slot];
    int result = vfs_write(file->node, file->offset, (uint32_t)size, buffer);
    if (result > 0) file->offset += (uint32_t)result;
    return result;
}

int process_file_unlink(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;
    return vfs_delete(resolved) == VFS_OK ? 0 : -1;
}

int process_file_close(Process *process, int descriptor) {
    int slot = descriptor - PROCESS_FD_BASE;
    if (process == NULL || slot < 0 || slot >= MAX_PROCESS_FILES ||
        !process->files[slot].in_use) {
        return -1;
    }

    process_file_t *file = &process->files[slot];
    int result = vfs_close(file->node);
    if (result != VFS_OK) return -1;
    memset(file, 0, sizeof(*file));
    return 0;
}

void process_close_all_files(Process *process) {
    if (process == NULL) return;
    for (int i = 0; i < MAX_PROCESS_FILES; ++i) {
        if (process->files[i].in_use) {
            (void)vfs_close(process->files[i].node);
            memset(&process->files[i], 0, sizeof(process->files[i]));
        }
    }
}

void wait_for_process(int pid) {
    if (pid < 0) return;

    for (;;) {
        bool running = false;
        uint32_t flags = irq_save();
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if (process_list[i].pid == pid && process_list[i].is_running) {
                running = true;
                break;
            }
        }
        irq_restore(flags);
        if (!running) return;

        /* The APIC timer schedules the userspace task. HLT avoids burning the
         * shell's kernel context while it waits for the foreground process. */
        __asm__ __volatile__("hlt");
    }
}

int process_spawn(Process *parent, const char *path) {
    if (parent == NULL || path == NULL || *path == '\0') return -1;
    const char *arguments[] = {path};
    return process_spawn_args(parent, path, 1, arguments);
}

int process_spawn_args(Process *parent, const char *path, int argc,
                       const char *const *argv) {
    if (parent == NULL || path == NULL || *path == '\0' || argc < 1 ||
        argc > 32 || argv == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(parent, path, resolved) != 0) return -1;
    int pid = create_process_for_file_args(resolved, argc, argv,
                                           parent->working_directory);
    if (pid < 0) return -1;

    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (process_list[i].is_running && process_list[i].pid == pid) {
            process_list[i].parent_pid = parent->pid;
            break;
        }
    }
    irq_restore(flags);
    return pid;
}

int process_wait_status(Process *parent, int pid, int *status) {
    if (parent == NULL || status == NULL || pid <= 0) return -1;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        Process *child = &process_list[i];
        if (child->pid != pid || child->parent_pid != parent->pid) continue;
        if (child->is_running) {
            irq_restore(flags);
            return 0;
        }
        if (!child->has_exited) {
            irq_restore(flags);
            return -1;
        }
        *status = child->exit_status;
        child->has_exited = false;
        child->parent_pid = 0;
        irq_restore(flags);
        return 1;
    }
    irq_restore(flags);
    return -1;
}

void process_orphan_children(int parent_pid) {
    if (parent_pid <= 0) return;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (process_list[i].parent_pid == parent_pid) {
            process_list[i].parent_pid = 0;
            if (!process_list[i].is_running) {
                process_list[i].has_exited = false;
            }
        }
    }
    irq_restore(flags);
}

int process_get_info(uint32_t index, process_info_t* info) {
    if (info == NULL) return -1;
    uint32_t flags = irq_save();
    uint32_t visible = 0;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        Process* process = &process_list[i];
        if (!process->is_running && !process->has_exited) continue;
        if (visible++ != index) continue;

        memset(info, 0, sizeof(*info));
        info->pid = process->pid;
        info->parent_pid = process->parent_pid;
        info->exit_status = process->exit_status;
        strncpy(info->name, process->name, sizeof(info->name) - 1U);
        if (process->has_exited) {
            info->state = PROCESS_STATE_ZOMBIE;
        } else if (process->task_id >= 0 && process->task_id < num_tasks) {
            int task_state = tasks[process->task_id].status;
            if (task_state == TASK_RUNNING) info->state = PROCESS_STATE_RUNNING;
            else if (task_state == TASK_SLEEPING) info->state = PROCESS_STATE_SLEEPING;
            else if (task_state == TASK_WAITING) info->state = PROCESS_STATE_WAITING;
            else info->state = PROCESS_STATE_READY;
        } else {
            info->state = PROCESS_STATE_READY;
        }
        irq_restore(flags);
        return 1;
    }
    irq_restore(flags);
    return 0;
}

int process_terminate(int pid) {
    if (pid <= 0) return -1;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running && process_list[i].pid == pid) {
            int task_id = process_list[i].task_id;
            if (task_id < 0 || task_id >= num_tasks) {
                irq_restore(flags);
                return -1;
            }
            irq_restore(flags);
            scheduler_terminate_task(task_id);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}
