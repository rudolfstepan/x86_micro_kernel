/**
 * @file syscall_table.c
 * @brief System call table and handler implementation
 * 
 * This module provides the kernel's system call interface, allowing
 * user programs to request kernel services via INT 0x80.
 */

#include <stdbool.h>
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/sys.h"
#include "drivers/video/display.h"
#include "drivers/video/framebuffer.h"
#include "drivers/char/kb.h"
#include "drivers/char/rtc.h"
#include "drivers/bus/drives.h"
#include "kernel/time/pit.h"
#include "kernel/sched/scheduler.h"
#include "kernel/proc/process.h"
#include "arch/x86/mm/paging.h"
#include "fs/vfs/vfs.h"
#include "mm/kmalloc.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"  // For SYS_MALLOC, SYS_FREE, SYS_REALLOC, etc.
#include "lib/libc/string.h"

//---------------------------------------------------------------------------------------------
// Syscall Entry Points
//---------------------------------------------------------------------------------------------

/**
 * Simple kernel greeting function for syscall testing
 */
void kernel_hello(void) {
    printf("Hello from the kernel. All engines running.\n");
}

/**
 * Print a number from userspace for syscall testing
 */
void kernel_print_number(int number) {
    printf("Kernel received number: %d\n", number);
}

static uint32_t syscall_get_date(void) {
    int year, month, day;
    read_date(&year, &month, &day);
    return ((uint32_t)year << 16) | ((uint32_t)month << 8) | (uint32_t)day;
}

static uint32_t syscall_get_time(void) {
    int hours, minutes, seconds;
    read_time(&hours, &minutes, &seconds);
    return ((uint32_t)hours << 16) | ((uint32_t)minutes << 8) |
           (uint32_t)seconds;
}

static uint32_t syscall_memory_kb(void) {
    memory_stats_t stats;
    memory_get_stats(&stats);
    uint64_t kibibytes = stats.managed_bytes / 1024U;
    return kibibytes > UINT32_MAX ? UINT32_MAX : (uint32_t)kibibytes;
}

static int syscall_delay(const Registers *regs, uint32_t milliseconds) {
    if (milliseconds == 0) return 0;
    if (regs != NULL && (regs->cs & 3U) == 3U) {
        return scheduler_sleep_ms(milliseconds);
    }
    /* Early initialization and the rescue shell execute outside a scheduled
     * task and therefore cannot block on the scheduler. */
    pit_delay(milliseconds);
    return 0;
}

static int syscall_monotonic_ms(uint64_t *user_value) {
    uint64_t value = pit_monotonic_ms();
    return copy_to_user(user_value, &value, sizeof(value)) == 0 ? 0 : -14;
}

static int syscall_memory_stats(memory_stats_t *user_stats,
                                uint32_t user_size, uint32_t version) {
    if (version != MEMORY_STATS_VERSION || user_size < sizeof(memory_stats_t)) {
        return -22;
    }
    memory_stats_t stats;
    memory_get_stats(&stats);
    return copy_to_user(user_stats, &stats, sizeof(stats)) == 0 ? 0 : -14;
}

typedef struct {
    uint32_t version;
    uint32_t struct_size;
} syscall_abi_header_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t rgb;
} syscall_display_rect_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    int32_t x;
    int32_t y;
    uint32_t foreground_rgb;
    uint32_t background_rgb;
    uint32_t text_address;
    uint32_t text_length;
} syscall_display_text_t;

_Static_assert(sizeof(framebuffer_display_info_t) == 56U,
               "display information ABI size changed");
_Static_assert(sizeof(syscall_display_rect_t) == 28U,
               "display rectangle ABI size changed");
_Static_assert(sizeof(syscall_display_text_t) == 32U,
               "display text ABI size changed");

static int syscall_display_info(framebuffer_display_info_t *user_info) {
    if (!framebuffer_available()) return -19; /* ENODEV */
    syscall_abi_header_t header;
    if (copy_from_user(&header, user_info, sizeof(header)) != 0) return -14;
    if (header.version != FRAMEBUFFER_DISPLAY_ABI_VERSION ||
        header.struct_size < sizeof(framebuffer_display_info_t)) return -22;

    framebuffer_display_info_t info;
    if (!framebuffer_get_display_info(&info)) return -19;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 0 : -14;
}

static int syscall_display_fill_rect(const syscall_display_rect_t *user_rect) {
    if (!framebuffer_available()) return -19; /* ENODEV */
    syscall_display_rect_t rect;
    if (copy_from_user(&rect, user_rect, sizeof(rect)) != 0) return -14;
    if (rect.version != FRAMEBUFFER_DISPLAY_ABI_VERSION ||
        rect.struct_size < sizeof(rect) ||
        (rect.rgb & 0xFF000000U) != 0) return -22;

    /* Rendering is deliberately preemptible.  The framebuffer geometry is
     * immutable after boot and occasional visual tearing is preferable to a
     * Ring-3 caller monopolizing the global UP scheduler. */
    bool drawn = framebuffer_fill_rect(rect.x, rect.y, rect.width,
                                       rect.height, rect.rgb);
    return drawn ? 0 : -19;
}

static int syscall_display_draw_text(const syscall_display_text_t *user_text) {
    if (!framebuffer_available()) return -19; /* ENODEV */
    syscall_display_text_t request;
    if (copy_from_user(&request, user_text, sizeof(request)) != 0) return -14;
    if (request.version != FRAMEBUFFER_DISPLAY_ABI_VERSION ||
        request.struct_size < sizeof(request) ||
        request.text_length > FRAMEBUFFER_DISPLAY_MAX_TEXT ||
        (request.foreground_rgb & 0xFF000000U) != 0 ||
        (request.background_rgb & 0xFF000000U) != 0) return -22;
    if (request.text_length == 0) return 0;
    if (!user_range_accessible(paging_current_directory(),
                               request.text_address, request.text_length,
                               false)) return -14;

    char text[FRAMEBUFFER_DISPLAY_MAX_TEXT];
    if (copy_from_user(text, (const void*)(uintptr_t)request.text_address,
                       request.text_length) != 0) return -14;
    bool drawn = framebuffer_draw_text_pixels(
        request.x, request.y, text, request.text_length,
        request.foreground_rgb, request.background_rgb);
    return drawn ? (int)request.text_length : -19;
}

static int syscall_terminal_write(const char *user_buffer, size_t size) {
    if (size > INT_MAX ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer,
                               size, false)) return -14;
    char buffer[256];
    size_t total = 0;
    while (total < size) {
        size_t amount = size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (copy_from_user(buffer, user_buffer + total, amount) != 0) return -14;
        for (size_t index = 0; index < amount; ++index)
            display_putchar(buffer[index]);
        total += amount;
    }
    return (int)total;
}

static int syscall_terminal_draw(uint32_t position, const char *user_buffer,
                                 size_t size) {
    uint32_t column = position & 0xFFFFU;
    uint32_t row = position >> 16;
    if (column >= 80U || row >= 25U || size > 80U - column) return -22;
    if (size == 0) return 0;
    if (!user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer,
                               size, false)) return -14;
    char buffer[80];
    if (copy_from_user(buffer, user_buffer, size) != 0) return -14;
    display_write_at((int)column, (int)row, buffer, (unsigned int)size);
    return (int)size;
}

static int syscall_open(const char *user_path) {
    char path[256];
    Process *process = scheduler_current_process();
    if (process == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) {
        return -14; /* EFAULT */
    }
    int descriptor = process_file_open(process, path);
    return descriptor < 0 ? -2 : descriptor; /* ENOENT/resource failure */
}

static int syscall_read(int descriptor, void *user_buffer, size_t size) {
    Process *process = scheduler_current_process();
    if (process == NULL) return -9; /* EBADF */
    if (size == 0) return 0;
    if (size > INT_MAX ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer, size, true)) {
        return -14; /* EFAULT */
    }

    uint8_t buffer[512];
    size_t total = 0;
    while (total < size) {
        size_t amount = size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        int result = process_file_read(process, descriptor, buffer, amount);
        if (result < 0) return total != 0 ? (int)total : -9;
        if (result == 0) break;
        if (copy_to_user((uint8_t*)user_buffer + total, buffer,
                         (size_t)result) != 0) {
            return -14;
        }
        total += (size_t)result;
        if ((size_t)result < amount) break;
    }
    return (int)total;
}

static int syscall_close(int descriptor) {
    Process *process = scheduler_current_process();
    return process_file_close(process, descriptor) == 0 ? 0 : -9;
}

typedef struct {
    char name[256];
    uint32_t type;
    uint32_t size;
} syscall_file_info_t;

static int syscall_copy_path(char resolved[PROCESS_PATH_MAX],
                             const char *user_path) {
    char path[PROCESS_PATH_MAX];
    Process *process = scheduler_current_process();
    if (process == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) return -14;
    return process_resolve_path(process, path, resolved) == 0 ? 0 : -22;
}

static int syscall_copy_file_info(void *user_info,
                                  const vfs_dir_entry_t *entry) {
    syscall_file_info_t info;
    memset(&info, 0, sizeof(info));
    strncpy(info.name, entry->name, sizeof(info.name) - 1U);
    info.type = (uint32_t)entry->type;
    info.size = entry->size;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 0 : -14;
}

static int syscall_stat(const char *user_path, void *user_info) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    vfs_dir_entry_t entry;
    result = vfs_stat(path, &entry);
    if (result != VFS_OK) return -2;
    return syscall_copy_file_info(user_info, &entry);
}

static int syscall_readdir(const char *user_path, uint32_t index,
                           void *user_info) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    vfs_dir_entry_t entry;
    result = vfs_readdir(path, index, &entry);
    if (result == VFS_ERR_NOT_FOUND) return 0;
    if (result != VFS_OK) return -2;
    result = syscall_copy_file_info(user_info, &entry);
    return result == 0 ? 1 : result;
}

#define SYSCALL_READDIR_BATCH_CAPACITY 4U
static int syscall_readdir_batch(const char *user_path, uint32_t index,
                                 void *user_entries) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    syscall_file_info_t info[SYSCALL_READDIR_BATCH_CAPACITY];
    vfs_dir_entry_t entries[SYSCALL_READDIR_BATCH_CAPACITY];
    result = vfs_readdir_batch(path, index, entries,
                               SYSCALL_READDIR_BATCH_CAPACITY);
    if (result < 0) return -5;
    if (result == 0) return 0;
    for (int i = 0; i < result; ++i) {
        memset(&info[i], 0, sizeof(info[i]));
        strncpy(info[i].name, entries[i].name, sizeof(info[i].name) - 1U);
        info[i].type = (uint32_t)entries[i].type;
        info[i].size = entries[i].size;
    }
    size_t bytes = (size_t)result * sizeof(info[0]);
    return copy_to_user(user_entries, info, bytes) == 0 ? result : -14;
}

static int syscall_create(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    Process *process = scheduler_current_process();
    int descriptor = process_file_create(process, path);
    return descriptor < 0 ? -5 : descriptor;
}

static int syscall_write(int descriptor, const void *user_buffer, size_t size) {
    Process *process = scheduler_current_process();
    if (process == NULL) return -9;
    if (size == 0) return 0;
    if (size > INT_MAX ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_buffer, size, false)) {
        return -14;
    }
    uint8_t buffer[512];
    size_t total = 0;
    while (total < size) {
        size_t amount = size - total;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (copy_from_user(buffer, (const uint8_t*)user_buffer + total,
                           amount) != 0) return -14;
        int written = process_file_write(process, descriptor, buffer, amount);
        if (written < 0) return total != 0 ? (int)total : -9;
        if (written == 0) break;
        total += (size_t)written;
        if ((size_t)written < amount) break;
    }
    return (int)total;
}

static int syscall_unlink(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return process_file_unlink(scheduler_current_process(), path) == 0 ? 0 : -2;
}

static int syscall_rename(const char *user_old_path,
                          const char *user_new_path) {
    char old_path[PROCESS_PATH_MAX];
    char new_path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(old_path, user_old_path);
    if (result != 0) return result;
    result = syscall_copy_path(new_path, user_new_path);
    if (result != 0) return result;
    return vfs_rename(old_path, new_path) == VFS_OK ? 0 : -5;
}

static int syscall_getpid(void) {
    Process *process = scheduler_current_process();
    return process != NULL ? process->pid : -3;
}

static int syscall_spawn(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    Process *parent = scheduler_current_process();
    if (parent == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) return -14;
    int pid = process_spawn(parent, path);
    return pid < 0 ? -2 : pid;
}

#define SYSCALL_MAX_ARGUMENTS 16
#define SYSCALL_ARGUMENT_CAPACITY 256
static int syscall_spawnv(const char *user_path, const char *const *user_argv,
                          int argc) {
    if (argc < 1 || argc > SYSCALL_MAX_ARGUMENTS || user_argv == NULL) {
        return -22;
    }
    char path[PROCESS_PATH_MAX];
    char *arguments = (char*)k_malloc(
        (size_t)argc * SYSCALL_ARGUMENT_CAPACITY);
    const char *argument_list[SYSCALL_MAX_ARGUMENTS];
    if (arguments == NULL) return -12;

    Process *parent = scheduler_current_process();
    if (parent == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) {
        k_free(arguments);
        return -14;
    }

    for (int index = 0; index < argc; ++index) {
        const char *user_argument;
        char *argument = arguments +
                         (size_t)index * SYSCALL_ARGUMENT_CAPACITY;
        if (copy_from_user(&user_argument, user_argv + index,
                           sizeof(user_argument)) != 0 ||
            copy_string_from_user(argument, SYSCALL_ARGUMENT_CAPACITY,
                                  user_argument) < 0) {
            k_free(arguments);
            return -14;
        }
        argument_list[index] = argument;
    }
    int result = process_spawn_args(parent, path, argc, argument_list);
    k_free(arguments);
    return result;
}

static int syscall_wait(int pid, int *user_status) {
    Process *parent = scheduler_current_process();
    if (parent == NULL ||
        !user_range_accessible(paging_current_directory(),
                               (uint32_t)(uintptr_t)user_status,
                               sizeof(*user_status), true)) return -14;
    for (;;) {
        int status = 0;
        /* On this uniprocessor kernel, keeping interrupts disabled makes the
         * child-state check and TASK_WAITING registration one atomic
         * operation.  Otherwise a child can exit between both operations and
         * its wakeup is lost permanently. */
        uint32_t flags = irq_save();
        wait_queue_t *wait_queue = NULL;
        int result = process_wait_status_locked(parent, pid, &status,
                                                &wait_queue);
        if (result < 0) {
            irq_restore(flags);
            return -10;
        }
        if (result > 0) {
            irq_restore(flags);
            (void)scheduler_reap_finished_tasks();
            return copy_to_user(user_status, &status, sizeof(status)) == 0
                       ? pid : -14;
        }
        if (wait_queue == NULL ||
            wait_queue_block_locked(wait_queue, TASK_BLOCK_WAITING) != 0) {
            irq_restore(flags);
            return -11;
        }
        irq_restore(flags);
    }
}

static int syscall_process_info(uint32_t index, void *user_info) {
    process_info_t info;
    int result = process_get_info(index, &info);
    if (result <= 0) return result;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 1 : -14;
}

static int syscall_kill(int pid) {
    Process *caller = scheduler_current_process();
    if (caller == NULL || pid <= 0 || pid == caller->pid) return -22;
    return process_terminate(pid) == 0 ? 0 : -3;
}

static int syscall_getcwd(void *user_buffer, size_t size) {
    if (size == 0 || size > PROCESS_PATH_MAX) return -22;
    char path[PROCESS_PATH_MAX];
    if (process_get_working_directory(scheduler_current_process(), path,
                                      size) != 0) return -34;
    size_t length = strlen(path) + 1U;
    return copy_to_user(user_buffer, path, length) == 0 ? 0 : -14;
}

static int syscall_chdir(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    Process *process = scheduler_current_process();
    if (process == NULL ||
        copy_string_from_user(path, sizeof(path), user_path) < 0) return -14;
    return process_set_working_directory(process, path) == 0 ? 0 : -2;
}

typedef struct {
    uint32_t type;
    char name[8];
    char mount_point[64];
} syscall_drive_info_t;

static int syscall_drive_info(uint32_t index, void *user_info) {
    if (index >= (uint32_t)drive_count) return 0;
    drive_t *drive = &detected_drives[index];
    if (drive->mount_point[0] == '\0') return -2;
    syscall_drive_info_t info;
    memset(&info, 0, sizeof(info));
    info.type = (uint32_t)drive->type;
    strncpy(info.name, drive->name, sizeof(info.name) - 1U);
    strncpy(info.mount_point, drive->mount_point,
            sizeof(info.mount_point) - 1U);
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 1 : -14;
}

static int syscall_space(const char *user_path, void *user_info) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    vfs_space_info_t info;
    result = vfs_space(path, &info);
    if (result != VFS_OK) return -5;
    return copy_to_user(user_info, &info, sizeof(info)) == 0 ? 0 : -14;
}

static int syscall_mkdir(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return vfs_mkdir(path) == VFS_OK ? 0 : -5;
}

static int syscall_rmdir(const char *user_path) {
    char path[PROCESS_PATH_MAX];
    int result = syscall_copy_path(path, user_path);
    if (result != 0) return result;
    return vfs_rmdir(path) == VFS_OK ? 0 : -5;
}

//---------------------------------------------------------------------------------------------
// System Call Table
//---------------------------------------------------------------------------------------------

/**
 * Global syscall dispatch table - maps syscall numbers to function pointers
 * User programs trigger INT 0x80 with syscall number in EAX
 */
void* syscall_table[512] __attribute__((section(".syscall_table"))) = {
    (void*)&display_putchar,            // Syscall 0: Write character to display
    (void*)&kernel_print_number,        // Syscall 1: Print number (for testing)
    (void*)&scheduler_sleep_ms,         // Syscall 2: Compatible blocking delay
    (void*)&kb_wait_enter,              // Syscall 3: Wait for Enter key
    (void*)&process_user_malloc,        // Syscall 4: Process-local allocation
    (void*)&process_user_free,          // Syscall 5: Release user allocation
    (void*)&process_user_realloc,       // Syscall 6: Resize user allocation
    (void*)&getchar,                    // Syscall 7: Read character from keyboard
    NULL,                               // Syscall 8: reserved (IRQ registration is privileged)
    (void*)&task_exit,                  // Syscall 9: Terminate current task
    (void*)&syscall_get_date,           // Syscall 10: Packed RTC date
    (void*)&syscall_get_time,           // Syscall 11: Packed RTC time
    (void*)&pit_ticks,                  // Syscall 12: Milliseconds since boot
    (void*)&syscall_memory_kb,          // Syscall 13: Usable memory in KiB
    (void*)&syscall_open,               // Syscall 14: Open read-only file
    (void*)&syscall_read,               // Syscall 15: Read from descriptor
    (void*)&syscall_close,              // Syscall 16: Close descriptor
    (void*)&syscall_stat,               // Syscall 17: Get path metadata
    (void*)&syscall_readdir,            // Syscall 18: Read directory entry
    (void*)&syscall_create,             // Syscall 19: Create writable file
    (void*)&syscall_write,              // Syscall 20: Write descriptor
    (void*)&syscall_unlink,             // Syscall 21: Delete file
    (void*)&syscall_getpid,             // Syscall 22: Current process ID
    (void*)&syscall_spawn,              // Syscall 23: Start child process
    (void*)&syscall_wait,               // Syscall 24: Collect child status
    (void*)&syscall_readdir_batch,      // Syscall 25: Read directory batch
    (void*)&syscall_process_info,       // Syscall 26: Enumerate processes
    (void*)&syscall_kill,               // Syscall 27: Terminate a process
    (void*)&syscall_getcwd,             // Syscall 28: Current directory
    (void*)&syscall_chdir,              // Syscall 29: Change directory
    (void*)&syscall_spawnv,             // Syscall 30: Spawn with arguments
    (void*)&syscall_drive_info,         // Syscall 31: Mounted drive metadata
    (void*)&syscall_space,              // Syscall 32: Filesystem capacity
    (void*)&syscall_mkdir,              // Syscall 33: Create directory
    (void*)&syscall_rmdir,              // Syscall 34: Remove directory
    (void*)&display_clear,              // Syscall 35: Clear terminal
    (void*)&display_set_cursor,         // Syscall 36: Set terminal cursor
    (void*)&syscall_terminal_write,     // Syscall 37: Write terminal buffer
    (void*)&syscall_terminal_draw,      // Syscall 38: Draw text at position
    (void*)&getchar_nonblocking,        // Syscall 39: Poll terminal input
    (void*)&scheduler_yield,            // Syscall 40: Yield current time slice
    (void*)&scheduler_sleep_ms,         // Syscall 41: Blocking sleep in ms
    (void*)&syscall_monotonic_ms,       // Syscall 42: 64-bit monotonic time
    (void*)&syscall_memory_stats,       // Syscall 43: Physical/heap metrics
    (void*)&syscall_display_info,       // Syscall 44: Versioned display info
    (void*)&syscall_display_fill_rect,  // Syscall 45: Clipped RGB rectangle
    (void*)&syscall_display_draw_text,  // Syscall 46: Clipped pixel text
    (void*)&syscall_rename,             // Syscall 47: Atomic same-FS rename
    // Add more syscalls here as needed
};

//---------------------------------------------------------------------------------------------
// System Call Handler
//---------------------------------------------------------------------------------------------

/**
 * Main syscall dispatcher - called from INT 0x80 handler in arch/x86/cpu/syscall.asm
 * 
 * Retrieves syscall number and arguments from CPU registers:
 * - EAX: syscall number
 * - EBX: argument 1
 * - ECX: argument 2
 * - EDX: argument 3
 * 
 * @param regs Saved register frame built by syscall_handler_asm
 */
void syscall_handler(Registers* regs) {
    const uint32_t syscall_index = regs->eax;
    const uint32_t arg1 = regs->ebx;
    const uint32_t arg2 = regs->ecx;
    const uint32_t arg3 = regs->edx;
    uint32_t result = 0;

    // Validate syscall index
    if (syscall_index >= 512 || syscall_table[syscall_index] == 0) {
        printf("Invalid syscall index: %u\n", syscall_index);
        regs->eax = (uint32_t)-1;
        return;
    }

    switch (syscall_index) {
        case SYS_TERMINAL_PUTCHAR:
            display_putchar((char)arg1);
            break;
        case SYS_PRINT:
            kernel_print_number((int)arg1);
            break;
        case SYS_DELAY:
            result = (uint32_t)syscall_delay(regs, arg1);
            break;
        case SYS_WAIT_ENTER:
            kb_wait_enter();
            break;
        case SYS_MALLOC:
            result = (uint32_t)(uintptr_t)process_user_malloc((size_t)arg1);
            if (result == 0) result = (uint32_t)-12; /* ENOMEM */
            break;
        case SYS_FREE:
            result = process_user_free((void*)(uintptr_t)arg1) == 0
                ? 0U : (uint32_t)-22; /* EINVAL */
            break;
        case SYS_REALLOC:
            result = (uint32_t)(uintptr_t)process_user_realloc(
                (void*)(uintptr_t)arg1, (size_t)arg2);
            if (result == 0 && arg2 != 0) result = (uint32_t)-12;
            break;
        case SYS_TERMINAL_GETCHAR:
            result = (uint32_t)(uint8_t)getchar();
            if (result == 0x03U) {
                printf("^C\n");
                task_exit_status(130);
            }
            break;
        case SYS_EXIT:
            task_exit_status((int)arg1);
        case SYS_GET_DATE:
            result = syscall_get_date();
            break;
        case SYS_GET_TIME:
            result = syscall_get_time();
            break;
        case SYS_UPTIME_MS:
            result = pit_ticks();
            break;
        case SYS_MEMORY_KB:
            result = syscall_memory_kb();
            break;
        case SYS_OPEN:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_open((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_READ:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_read((int)arg1,
                                            (void*)(uintptr_t)arg2,
                                            (size_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_CLOSE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_close((int)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_STAT:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_stat(
                (const char*)(uintptr_t)arg1, (void*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_READDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_readdir(
                (const char*)(uintptr_t)arg1, arg2, (void*)(uintptr_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_CREATE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_create((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_WRITE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_write(
                (int)arg1, (const void*)(uintptr_t)arg2, (size_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_UNLINK:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_unlink((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_GETPID:
            result = (uint32_t)syscall_getpid();
            break;
        case SYS_SPAWN:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_spawn((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_WAIT:
            result = (uint32_t)syscall_wait(
                (int)arg1, (int*)(uintptr_t)arg2);
            break;
        case SYS_READDIR_BATCH:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_readdir_batch(
                (const char*)(uintptr_t)arg1, arg2,
                (void*)(uintptr_t)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_PROCESS_INFO:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_process_info(
                arg1, (void*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_KILL:
            result = (uint32_t)syscall_kill((int)arg1);
            break;
        case SYS_GETCWD:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_getcwd(
                (void*)(uintptr_t)arg1, (size_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_CHDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_chdir(
                (const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_SPAWNV:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_spawnv(
                (const char*)(uintptr_t)arg1,
                (const char* const*)(uintptr_t)arg2, (int)arg3);
            scheduler_preempt_enable();
            break;
        case SYS_DRIVE_INFO:
            result = (uint32_t)syscall_drive_info(
                arg1, (void*)(uintptr_t)arg2);
            break;
        case SYS_SPACE:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_space(
                (const char*)(uintptr_t)arg1, (void*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        case SYS_MKDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_mkdir((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_RMDIR:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_rmdir((const char*)(uintptr_t)arg1);
            scheduler_preempt_enable();
            break;
        case SYS_CLEAR:
            display_clear();
            break;
        case SYS_SET_CURSOR:
            if (arg1 < 80U && arg2 < 25U)
                display_set_cursor((int)arg1, (int)arg2);
            else
                result = (uint32_t)-22;
            break;
        case SYS_TERMINAL_WRITE:
            result = (uint32_t)syscall_terminal_write(
                (const char*)(uintptr_t)arg1, (size_t)arg2);
            break;
        case SYS_TERMINAL_DRAW:
            result = (uint32_t)syscall_terminal_draw(
                arg1, (const char*)(uintptr_t)arg2, (size_t)arg3);
            break;
        case SYS_GETCHAR_NONBLOCKING:
            result = (uint32_t)(uint8_t)getchar_nonblocking();
            break;
        case SYS_YIELD:
            result = (uint32_t)scheduler_yield();
            break;
        case SYS_SLEEP_MS:
            result = (uint32_t)syscall_delay(regs, arg1);
            break;
        case SYS_MONOTONIC_MS:
            result = (uint32_t)syscall_monotonic_ms(
                (uint64_t*)(uintptr_t)arg1);
            break;
        case SYS_MEMORY_STATS:
            result = (uint32_t)syscall_memory_stats(
                (memory_stats_t*)(uintptr_t)arg1, arg2, arg3);
            break;
        case SYS_DISPLAY_INFO:
            result = (uint32_t)syscall_display_info(
                (framebuffer_display_info_t*)(uintptr_t)arg1);
            break;
        case SYS_FILL_RECT:
            result = (uint32_t)syscall_display_fill_rect(
                (const syscall_display_rect_t*)(uintptr_t)arg1);
            break;
        case SYS_DRAW_TEXT:
            result = (uint32_t)syscall_display_draw_text(
                (const syscall_display_text_t*)(uintptr_t)arg1);
            break;
        case SYS_RENAME:
            scheduler_preempt_disable();
            result = (uint32_t)syscall_rename(
                (const char*)(uintptr_t)arg1,
                (const char*)(uintptr_t)arg2);
            scheduler_preempt_enable();
            break;
        default:
            result = (uint32_t)-1;
            break;
    }

    regs->eax = result;
}
