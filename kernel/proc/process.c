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
    memset((void*)(uintptr_t)address, 0, loaded_size);
    uint32_t offset = 0;
    while (offset < loaded_size) {
        uint32_t amount = loaded_size - offset;
        if (amount > 4096U) amount = 4096U;
        result = vfs_read(node, offset, amount,
                          (uint8_t*)(uintptr_t)(address + offset));
        if (result <= 0 || (uint32_t)result > amount) {
            (void)vfs_close(node);
            return -1;
        }
        offset += (uint32_t)result;
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
        if (!process_list[i].is_running) {
            Process *process = &process_list[i];
            int pid = allocate_pid_locked();
            if (pid < 0) {
                irq_restore(flags);
                return -1;
            }
            process->pid = pid;
            process->task_id = -1;
            process->uses_shared_program_image = shared_image;
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
    if (filename == NULL || *filename == '\0') {
        return -1;
    }

    int slot = claim_process_slot(filename, false);
    if (slot < 0) {
        printf("Error: Maximum number of running programs reached.\n");
        return -1;
    }

    Process *process = &process_list[slot];
    int pid = process->pid;
    int loaded_size = load_program_into_memory(filename, PROGRAM_STAGING_ADDRESS);
    if (loaded_size < 0) {
        release_process_slot(process);
        return -1;
    }

    program_header_t* header = (program_header_t*)PROGRAM_STAGING_ADDRESS;
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
                         (uint32_t)loaded_size) != 0) {
        printf("Invalid relocation table in '%s'.\n", filename);
        release_process_slot(process);
        return -1;
    }

    page_directory_t *page_directory = create_page_directory();
    if (page_directory == NULL) {
        release_process_slot(process);
        return -1;
    }

    uint32_t mapped_size = ((uint32_t)loaded_size + PAGE_SIZE - 1U) &
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
        uint32_t amount = (uint32_t)loaded_size - offset;
        if (amount > PAGE_SIZE) amount = PAGE_SIZE;
        if (offset < (uint32_t)loaded_size) {
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

    int task_id = create_user_task(
        header->entry_point + USER_PROGRAM_ADDRESS, USER_STACK_TOP,
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

void list_running_processes(void) {
    printf("Running programs:\n");
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running) {
            printf("PID %d: %s\n", process_list[i].pid, process_list[i].name);
        }
    }
}

void terminate_process(int pid) {
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running && process_list[i].pid == pid) {
            if (process_list[i].task_id < 0) {
                irq_restore(flags);
                printf("Program with PID %d is still loading.\n", pid);
                return;
            }

            // Terminate the task associated with the process
            scheduler_terminate_task(process_list[i].task_id);
            process_list[i].is_running = false;
            process_list[i].task_id = -1;
            process_list[i].uses_shared_program_image = false;

            char name[sizeof(process_list[i].name)];
            strncpy(name, process_list[i].name, sizeof(name));
            name[sizeof(name) - 1U] = '\0';
            irq_restore(flags);
            printf("Program '%s' with PID %d terminated.\n", name, pid);
            return;
        }
    }

    irq_restore(flags);
    printf("Error: PID %d not found.\n", pid);
}
