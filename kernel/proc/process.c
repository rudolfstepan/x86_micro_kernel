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
#include "drivers/bus/drives.h"
#include "kernel/init/prg.h"
#include "kernel/sched/scheduler.h"
#include "include/kernel/panic.h"
#include "include/kernel/storage_request_pool.h"

#define USER_PROGRAM_ADDRESS PROGRAM_V1_BASE
#define PROGRAM_REGION_SIZE PROGRAM_V1_REGION_SIZE

_Static_assert(PROGRAM_V1_BASE == USER_BASE,
               "MYPR v1 base must match the user address-space base");
_Static_assert(USER_STACK_LOWER_GUARD >= USER_HEAP_TOP,
               "User stack guards must not overlap the user heap");
_Static_assert(USER_STACK_UPPER_GUARD + PAGE_SIZE == USER_TOP,
               "Upper user stack guard must terminate at USER_TOP");

static void program_load_root_details(uint32_t *identity,
                                      uint32_t *location) {
    if (identity == NULL || location == NULL) return;
    *identity = UINT32_MAX;
    *location = (uint32_t)(uint16_t)drive_count;
    for (short index = 0; index < drive_count; ++index) {
        drive_t *drive = &detected_drives[index];
        if (strcmp(drive->mount_point, "/") != 0) continue;
        *identity = (uint32_t)(uint16_t)index;
        *location = drive->lba_offset;
        return;
    }
}

static int load_program_file(const char *program_name, uint8_t **image_out) {
    panic_context_set("program-load", "program loader", "open",
                      program_name);
    if (image_out == NULL) {
        panic_context_set_result(-22, 0U, 0U);
        return -1;
    }
    *image_out = NULL;
    vfs_node_t* node = NULL;
    int result = vfs_open(program_name, &node);
    if (result != VFS_OK || !node) {
        uint32_t identity, location;
        program_load_root_details(&identity, &location);
        panic_context_set_result(result, identity, location);
        printf("Program load open failed: %s (%d)\n", program_name, result);
        return -1;
    }
    if (node->type != VFS_FILE || node->size < sizeof(program_header_t) ||
        node->size > PROGRAM_REGION_SIZE) {
        panic_context_set_result(-8, (uint32_t)node->type, node->size);
        printf("Program load metadata invalid: %s (type=%u, size=%u)\n",
               program_name, (uint32_t)node->type, node->size);
        (void)vfs_close(node);
        return -1;
    }

    uint32_t loaded_size = node->size;
    panic_context_set("program-load", "program loader", "allocate image",
                      program_name);
    uint8_t *image = (uint8_t*)k_malloc(loaded_size);
    if (image == NULL) {
        panic_context_set_result(-12, loaded_size, 0U);
        printf("Program load allocation failed: %s (%u bytes)\n",
               program_name, loaded_size);
        (void)vfs_close(node);
        return -1;
    }
    /* Read the image sequentially in one VFS operation.  Restarting a FAT
     * read every page would walk the cluster chain from its beginning for
     * each chunk and makes larger programs progressively slower to start. */
    panic_context_set("program-load", "program loader", "read image",
                      program_name);
    result = vfs_read(node, 0, loaded_size, image);
    if (result != (int)loaded_size) {
        panic_context_set_result(result, loaded_size, 0U);
        printf("Program load read failed: %s (%d/%u bytes)\n",
               program_name, result, loaded_size);
        (void)vfs_close(node);
        k_free(image);
        return -1;
    }
    int close_result = vfs_close(node);
    panic_context_set("program-load", "program loader", "validate image",
                      program_name);
    int validation_result = program_image_validate(
        image, loaded_size, PROGRAM_REGION_SIZE);
    if (close_result != VFS_OK || validation_result != 0) {
        panic_context_set_result(validation_result, (uint32_t)close_result,
                                 loaded_size);
        printf("Program load validation failed: %s (close=%d, image=%d)\n",
               program_name, close_result, validation_result);
        k_free(image);
        return -1;
    }
    *image_out = image;
    return (int)loaded_size;
}

static void release_admission_image(uint8_t **image) {
    if (image == NULL || *image == NULL) return;
    k_free(*image);
    *image = NULL;
}


Process process_list[MAX_PROGRAMS];
int next_pid = 1; // PID counter starting at 1

static void profile_allow(process_domain_profile_t *profile,
                          uint32_t syscall_index) {
    if (syscall_index >= PROCESS_DOMAIN_SYSCALL_LIMIT) return;
    profile->allowed_syscalls[syscall_index / 32U] |=
        1U << (syscall_index % 32U);
}

static bool initialize_domain_profile(process_domain_profile_t *profile,
                                      process_domain_kind_t kind) {
    if (profile == NULL) return false;
    memset(profile, 0, sizeof(*profile));
    profile->version = PROCESS_DOMAIN_PROFILE_VERSION;
    profile->struct_size = sizeof(*profile);
    profile->kind = (uint32_t)kind;
    if (kind == PROCESS_DOMAIN_COMPATIBILITY) {
        for (uint32_t index = 0; index < PROCESS_DOMAIN_SYSCALL_LIMIT; ++index) {
            profile_allow(profile, index);
        }
        return true;
    }
    if (kind == PROCESS_DOMAIN_STORAGE) {
        static const uint8_t storage_syscalls[] = {
            SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
            SYS_STORAGE_BIND, SYS_STORAGE_CLAIM, SYS_STORAGE_BLOCK_READ,
            SYS_STORAGE_BLOCK_WRITE, SYS_STORAGE_MAINT_ACQUIRE,
            SYS_STORAGE_MAINT_RENEW, SYS_STORAGE_MAINT_RELEASE,
            SYS_STORAGE_COMPLETE
        };
        for (size_t index = 0;
             index < sizeof(storage_syscalls) / sizeof(storage_syscalls[0]);
             ++index) profile_allow(profile, storage_syscalls[index]);
        return true;
    }
    if (kind != PROCESS_DOMAIN_PROBE) return false;

    static const uint8_t probe_syscalls[] = {
        SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
        SYS_MEMORY_STATS, SYS_SCHEDULER_STATS,
        SYS_IPC_CREATE, SYS_IPC_SEND, SYS_IPC_RECEIVE,
        SYS_IPC_CLOSE,
        SYS_IPC_SEND_TIMEOUT, SYS_IPC_RECEIVE_TIMEOUT, SYS_REIST_REPORT,
        SYS_NETWORK_PROBE, SYS_NETWORK_PROBE_ID, SYS_REIST_ARP_BINDING,
        SYS_REIST_ARP_REPLY, SYS_REIST_ARP_RESOLUTION,
        SYS_REIST_ICMP_ECHO_REPLY, SYS_REIST_DHCP_COMMIT,
        SYS_REIST_UDP_ECHO_REPLY, SYS_REIST_UDP_BIND,
        SYS_REIST_UDP_UNBIND, SYS_REIST_UDP_REPLY, SYS_REIST_DHCP_RENEW,
        SYS_REIST_NETWORK_FRAME, SYS_REIST_UDP_INGRESS,
        SYS_REIST_DHCP_INGRESS, SYS_REIST_DHCP_BOOT_START,
        SYS_REIST_ICMP_INGRESS
    };
    for (size_t index = 0;
         index < sizeof(probe_syscalls) / sizeof(probe_syscalls[0]); ++index) {
        profile_allow(profile, probe_syscalls[index]);
    }
    return true;
}

bool process_syscall_allowed(const Process *process, uint32_t syscall_index) {
    if (process == NULL) return true; /* Explicit trusted kernel path. */
    const process_domain_profile_t *profile = &process->domain_profile;
    if (profile->version != PROCESS_DOMAIN_PROFILE_VERSION ||
        profile->struct_size != sizeof(*profile) ||
        syscall_index >= PROCESS_DOMAIN_SYSCALL_LIMIT ||
        (profile->kind != PROCESS_DOMAIN_COMPATIBILITY &&
         profile->kind != PROCESS_DOMAIN_PROBE &&
         profile->kind != PROCESS_DOMAIN_STORAGE)) return false;
    return (profile->allowed_syscalls[syscall_index / 32U] &
            (1U << (syscall_index % 32U))) != 0U;
}

static int allocate_pid_locked(void) {
    for (int attempt = 0; attempt <= MAX_PROGRAMS; ++attempt) {
        int candidate = next_pid;
        next_pid = next_pid == INT_MAX ? 1 : next_pid + 1;

        bool in_use = false;
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if ((process_list[i].is_running || process_list[i].has_exited) &&
                process_list[i].pid == candidate) {
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
    ipc_process_cleanup(process->pid, process->generation);
    storage_request_cancel_process(process->pid, process->generation);
    process_close_all_files(process);
    uint32_t flags = irq_save();
    process->is_running = false;
    process->uses_shared_program_image = false;
    process->task_id = -1;
    irq_restore(flags);
}

/* Reserve list state atomically, but do slow file/heap work with IRQs enabled. */
static int claim_process_slot(const char *name, bool shared_image,
                              int parent_pid, uint32_t parent_generation,
                              bool supervised,
                              process_domain_kind_t domain_kind) {
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

    size_t free_slots = 0U;
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (!process_list[i].is_running &&
            !(process_list[i].has_exited && process_list[i].parent_pid > 0) &&
            process_list[i].generation != UINT32_MAX) ++free_slots;
    }
    if (free_slots == 0U ||
        (!supervised && free_slots <= SUPERVISED_PROCESS_RESERVE)) {
        irq_restore(flags);
        return -1;
    }

    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (!process_list[i].is_running &&
            !(process_list[i].has_exited && process_list[i].parent_pid > 0)) {
            Process *process = &process_list[i];
            /* Generation wrap would make stale process identities valid
             * again.  Retire the slot instead of reusing generation zero. */
            if (process->generation == UINT32_MAX) continue;
            int pid = allocate_pid_locked();
            if (pid < 0) {
                irq_restore(flags);
                return -1;
            }
            uint32_t generation = process->generation + 1U;
            process->pid = pid;
            process->generation = generation;
            process->parent_pid = parent_pid;
            process->parent_generation = parent_generation;
            process->task_id = -1;
            process->exit_status = 0;
            process->has_exited = false;
            process->uses_shared_program_image = shared_image;
            process->heap_next = USER_HEAP_BASE;
            memset(process->user_allocations, 0,
                   sizeof(process->user_allocations));
            memset(process->files, 0, sizeof(process->files));
            if (!initialize_domain_profile(&process->domain_profile,
                                           domain_kind)) {
                process->is_running = false;
                irq_restore(flags);
                return -1;
            }
            memset(process->ipc_capabilities, 0,
                   sizeof(process->ipc_capabilities));
            wait_queue_init(&process->exit_waiters);
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
        if (length > 256U || stack < USER_STACK_BOTTOM + length) {
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

static int create_process_for_file_args_owned(const char *filename, int argc,
                                               const char *const *argv,
                                               const char *working_directory,
                                               Process *parent,
                                               bool supervised,
                                               process_domain_kind_t domain_kind) {
    if (filename == NULL || *filename == '\0') {
        return -1;
    }

    /* Reclaim stacks and address spaces before this spawn needs new frames.
     * Exit status remains in the Process zombie until its parent waits. */
    (void)scheduler_reap_finished_tasks();

    const char *display_name = filename;
    for (const char *cursor = filename; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') display_name = cursor + 1;
    }
    int parent_pid = parent != NULL ? parent->pid : 0;
    uint32_t parent_generation = parent != NULL ? parent->generation : 0U;
    int slot = claim_process_slot(display_name, false, parent_pid,
                                  parent_generation, supervised, domain_kind);
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
    uint8_t *program_image = NULL;
    int loaded_size = load_program_file(filename, &program_image);
    if (loaded_size < 0) {
        printf("Unable to load valid program '%s'\n", filename);
        release_process_slot(process);
        return -1;
    }

    program_header_t* header = (program_header_t*)program_image;
    uint32_t memory_image_size = (uint32_t)sizeof(*header) +
                                 header->program_size;
    uint32_t stored_image_size = header->relocation_offset;
    uint32_t entry_point = header->entry_point;

    uint32_t program_frames =
        (memory_image_size + PAGE_SIZE - 1U) / PAGE_SIZE;
    uint32_t stack_frames = (USER_STACK_TOP - USER_STACK_BOTTOM) / PAGE_SIZE;
    uint64_t required_frames = (uint64_t)program_frames + stack_frames + 16U;
    memory_stats_t memory_stats;
    memory_get_stats(&memory_stats);
    uint64_t reserve_frames = supervised
        ? 0U : SUPERVISED_RESTART_FRAME_RESERVE;
    if (required_frames + reserve_frames >
        memory_stats.free_frame_bytes / PAGE_SIZE) {
        release_admission_image(&program_image);
        release_process_slot(process);
        return -1;
    }

    page_directory_t *page_directory = create_page_directory();
    if (page_directory == NULL) {
        k_free(program_image);
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
            k_free(program_image);
            release_process_slot(process);
            return -1;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
        uint32_t amount = 0;
        if (offset < stored_image_size) {
            amount = stored_image_size - offset;
            if (amount > PAGE_SIZE) amount = PAGE_SIZE;
            memcpy((void*)(uintptr_t)frame,
                   program_image + offset,
                   amount);
        }
    }
    /* Fresh page directories leave both adjacent guard pages non-present.
     * Only the bounded stack interval is mapped user-writable. */
    for (uint32_t address = USER_STACK_BOTTOM;
         address < USER_STACK_TOP; address += PAGE_SIZE) {
        uint32_t frame = (uint32_t)allocate_frame();
        if (frame == 0 || map_page(page_directory, address, frame,
                                  PAGE_USER | PAGE_RW) != 0) {
            if (frame != 0) free_frame(frame);
            free_page_directory(page_directory);
            k_free(program_image);
            release_process_slot(process);
            return -1;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
    }
    k_free(program_image);

    uint32_t* kernel_stack = scheduler_allocate_kernel_stack();
    if (kernel_stack == NULL) {
        free_page_directory(page_directory);
        release_process_slot(process);
        return -1;
    }

    uint32_t user_stack;
    if (build_user_arguments(page_directory, argc, argv, &user_stack) != 0) {
        scheduler_free_kernel_stack(kernel_stack);
        free_page_directory(page_directory);
        release_process_slot(process);
        return -1;
    }

    int task_id = (supervised ? create_supervised_user_task : create_user_task)(
        entry_point + USER_PROGRAM_ADDRESS, user_stack,
        kernel_stack, page_directory, process);
    if (task_id < 0) {
        scheduler_free_kernel_stack(kernel_stack);
        free_page_directory(page_directory);
        release_process_slot(process);
        return -1;
    }
    return pid;
}

int create_process_for_file_args(const char *filename, int argc,
                                 const char *const *argv,
                                 const char *working_directory) {
    return create_process_for_file_args_owned(filename, argc, argv,
        working_directory, NULL, false, PROCESS_DOMAIN_COMPATIBILITY);
}

int create_process(void* entry_point) {
    if (entry_point == NULL) {
        return -1;
    }

    (void)scheduler_reap_finished_tasks();

    int slot = claim_process_slot("Unknown", false, 0, 0U, true,
                                  PROCESS_DOMAIN_COMPATIBILITY);
    if (slot < 0) {
        printf("Error: Maximum number of running programs reached.\n");
        return -1;
    }
    Process *process = &process_list[slot];
    int pid = process->pid;

    uint32_t* stack = scheduler_allocate_kernel_stack();
    if (stack == NULL) {
        printf("Error: Failed to allocate stack for process\n");
        release_process_slot(process);
        return -1;
    }

    int task_id = create_task((void (*)())entry_point, stack, process);
    if (task_id < 0) {
        scheduler_free_kernel_stack(stack);
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
        while (*cursor == '/' || *cursor == '\\') ++cursor;
        if (*cursor == '\0') break;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') ++cursor;
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

static const char* process_drive_mount(char letter) {
    if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
    for (int index = 0; index < drive_count; ++index) {
        drive_t* drive = &detected_drives[index];
        if (drive->mount_point[0] == '\0') continue;
        if (strcmp(drive->mount_point, "/") == 0) {
            if (letter == 'C') return drive->mount_point;
            continue;
        }
        if (strlen(drive->name) != 4U || drive->name[3] < '0' ||
            drive->name[3] > '9') continue;
        char mapped = 0;
        if (drive->type == DRIVE_TYPE_FDD) {
            mapped = (char)('A' + drive->name[3] - '0');
        } else if (drive->type == DRIVE_TYPE_ATA ||
                   drive->type == DRIVE_TYPE_AHCI) {
            mapped = (char)('C' + drive->name[3] - '0');
        }
        if (mapped == letter) return drive->mount_point;
    }
    return NULL;
}

int process_resolve_path(const Process *process, const char *path,
                         char resolved[PROCESS_PATH_MAX]) {
    if (process == NULL || path == NULL || path[0] == '\0') return -1;
    strcpy(resolved, "/");
    if (path[1] == ':') {
        const char* mount = process_drive_mount(path[0]);
        if (mount == NULL || strlen(mount) >= PROCESS_PATH_MAX) return -1;
        strcpy(resolved, mount);
        return append_path_components(resolved, PROCESS_PATH_MAX, path + 2);
    }
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

int process_file_sync(Process *process, int descriptor) {
    int slot = descriptor - PROCESS_FD_BASE;
    if (process == NULL || slot < 0 || slot >= MAX_PROCESS_FILES ||
        !process->files[slot].in_use || !process->files[slot].writable) {
        return -1;
    }
    return vfs_sync(process->files[slot].node) == VFS_OK ? 0 : -1;
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
    return create_process_for_file_args_owned(
        resolved, argc, argv, parent->working_directory, parent, false,
        PROCESS_DOMAIN_COMPATIBILITY);
}

int process_spawn_supervised(const char *path, int argc,
                             const char *const *argv,
                             process_domain_kind_t domain_kind) {
    if (path == NULL || *path == '\0' || argc < 1 || argc > 32 ||
        argv == NULL) return -1;
    return create_process_for_file_args_owned(path, argc, argv, "/", NULL,
                                               true, domain_kind);
}

int process_ipc_delegate(Process *source, ipc_handle_t handle,
                         int target_pid, uint32_t rights) {
    if (source == NULL || target_pid <= 0 || target_pid == source->pid) {
        return -22;
    }

    /* The preemption guard makes PID lookup and generation-scoped capability
     * publication one transaction on the current UP scheduler. */
    scheduler_preempt_disable();
    Process *target = NULL;
    uint32_t target_generation = 0U;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        if (process_list[index].is_running &&
            process_list[index].pid == target_pid) {
            target = &process_list[index];
            target_generation = process_list[index].generation;
            break;
        }
    }
    int result = target != NULL && target->generation == target_generation
        ? ipc_delegate(source, handle, target, rights) : -3;
    scheduler_preempt_enable();
    return result;
}

int process_ipc_delegate_identity(int source_pid, uint32_t source_generation,
                                  ipc_handle_t handle, Process *target,
                                  uint32_t rights) {
    if (source_pid <= 0 || source_generation == 0U || target == NULL)
        return -22;
    scheduler_preempt_disable();
    Process *source = NULL;
    for (size_t index = 0; index < MAX_PROGRAMS; ++index) {
        Process *candidate = &process_list[index];
        if (candidate->is_running && candidate->pid == source_pid &&
            candidate->generation == source_generation) {
            source = candidate;
            break;
        }
    }
    int result = source != NULL
        ? ipc_delegate(source, handle, target, rights) : -9;
    scheduler_preempt_enable();
    return result;
}

int process_wait_status_locked(Process *parent, int pid, int *status,
                               wait_queue_t **wait_queue) {
    if (parent == NULL || status == NULL || pid <= 0) return -1;
    if (wait_queue != NULL) *wait_queue = NULL;
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        Process *child = &process_list[i];
        if (child->pid != pid || child->parent_pid != parent->pid) continue;
        if (child->is_running) {
            if (wait_queue != NULL) *wait_queue = &child->exit_waiters;
            return 0;
        }
        if (!child->has_exited) {
            return -1;
        }
        *status = child->exit_status;
        if (child->task_id >= 0 &&
            scheduler_reap_finished_task_locked(child->task_id, child) == 0) {
            child->task_id = -1;
        }
        child->has_exited = false;
        child->parent_pid = 0;
        return 1;
    }
    return -1;
}

int process_wait_status(Process *parent, int pid, int *status) {
    uint32_t flags = irq_save();
    int result = process_wait_status_locked(parent, pid, status, NULL);
    irq_restore(flags);
    if (result > 0) (void)scheduler_reap_finished_tasks();
    return result;
}

void process_orphan_children(int parent_pid) {
    if (parent_pid <= 0) return;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (process_list[i].parent_pid == parent_pid) {
            process_list[i].parent_pid = 0;
            if (!process_list[i].is_running) {
                if (process_list[i].task_id >= 0 &&
                    scheduler_reap_finished_task_locked(
                        process_list[i].task_id, &process_list[i]) == 0) {
                    process_list[i].task_id = -1;
                }
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
        } else if (process->task_id >= 0 && process->task_id < num_tasks &&
                   tasks[process->task_id].process == process &&
                   tasks[process->task_id].process_generation ==
                       process->generation) {
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
    KASSERT_NOT_IRQ();
    scheduler_preempt_disable();
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running && process_list[i].pid == pid) {
            int task_id = process_list[i].task_id;
            if (task_id < 0 || task_id >= num_tasks ||
                tasks[task_id].process != &process_list[i] ||
                tasks[task_id].process_generation !=
                    process_list[i].generation) {
                irq_restore(flags);
                scheduler_preempt_enable();
                return -1;
            }
            irq_restore(flags);
            if (task_id == current_task) {
                scheduler_preempt_enable();
                task_exit_status(143);
            }
            /* The preemption guard preserves PID -> slot -> generation while
             * the scheduler closes the target's files with IRQs enabled. */
            scheduler_terminate_task(task_id);
            scheduler_preempt_enable();
            return 0;
        }
    }
    irq_restore(flags);
    scheduler_preempt_enable();
    return -1;
}

int process_terminate_authorized(Process *requester, int pid) {
    if (requester == NULL || pid <= 0) return -1;
    KASSERT_NOT_IRQ();
    scheduler_preempt_disable();
    uint32_t flags = irq_save();
    bool authorized = false;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        Process *target = &process_list[index];
        if (target->is_running && target->pid == pid &&
            target->parent_pid == requester->pid &&
            target->parent_generation == requester->generation) {
            authorized = true;
            break;
        }
    }
    irq_restore(flags);
    int result = authorized ? process_terminate(pid) : -1;
    scheduler_preempt_enable();
    return result;
}

int process_get_identity(int pid, uint32_t *generation_out) {
    if (pid <= 0 || generation_out == NULL) return -1;
    uint32_t flags = irq_save();
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        if (process_list[index].is_running && process_list[index].pid == pid) {
            *generation_out = process_list[index].generation;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

bool process_identity_alive(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return false;
    uint32_t flags = irq_save();
    bool alive = false;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        if (process_list[index].is_running && process_list[index].pid == pid &&
            process_list[index].generation == generation) {
            alive = true;
            break;
        }
    }
    irq_restore(flags);
    return alive;
}
