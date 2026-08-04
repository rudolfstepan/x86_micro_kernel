#include "kernel/shell/command.h"
#include "kernel/proc/process.h"
#include "kernel/init/prg.h"
#include "arch/x86/include/sys.h"
#include "kernel/sched/scheduler.h"
#include "kernel/shell/path_resolver.h"
#include "mm/kmalloc.h"

#include "drivers/char/rtc.h"
#include "drivers/block/ata.h"
#include "drivers/block/fdd.h"
#include "drivers/bus/drives.h"
#include "drivers/bus/pci.h"

#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "drivers/video/video.h"
#include "drivers/char/kb.h"
#include "kernel/time/pit.h"
#include "fs/vfs/filesystem.h"
#include "fs/vfs/vfs.h"
#include "fs/fat32/fat32.h"
#include "fs/fat12/fat12.h"
#include "drivers/net/rtl8139.h"
#include "drivers/net/e1000.h"
#include "drivers/net/ne2000.h"
#include "drivers/net/netstack.h"
#include "drivers/net/netdev.h"
// #include "drivers/net/vmxnet3.h"

char current_path[256] = "/";
static char shell_drive_paths[MAX_DRIVES][SHELL_PATH_MAX];
static bool shell_drive_path_initialized[MAX_DRIVES];
extern pci_device_t pci_devices[];
extern size_t pci_device_count;

// Forward declarations
int split_input(const char* input, char* command, char** arguments, int max_length, int max_args);
void open_file(const char* path);
void free_arguments(char** arguments, int arg_count);

static bool is_program_filename(const char *name) {
    size_t length;

    if (name == NULL) return false;
    length = strlen(name);
    if (length < 4U) return false;

    return name[length - 4U] == '.' &&
           (name[length - 3U] == 'p' || name[length - 3U] == 'P') &&
           (name[length - 2U] == 'r' || name[length - 2U] == 'R') &&
           (name[length - 1U] == 'g' || name[length - 1U] == 'G');
}

bool is_null_terminated(char* buffer, size_t max_length) {
    for (size_t i = 0; i < max_length; i++) {
        if (buffer[i] == '\0') {
            return true; // Found the null terminator
        }
    }
    return false; // No null terminator found within max_length
}

#define MAX_COMMANDS 100
#define MAX_LINE_LENGTH 256

typedef void (*command_func)(int cnt, const char **args);

// Command structure
typedef struct {
    const char *name;
    command_func execute;
} command_t;

// Forward declarations
void show_prompt(void);

// Command prototypes
void cmd_help(int cnt, const char **args);
void cmd_clear(int cnt, const char **args);
void cmd_echo(int cnt, const char **args);
void cmd_mem(int cnt, const char **args);
void cmd_dump(int cnt, const char **args);
void cmd_cls(int cnt, const char **args);
void cmd_ls(int cnt, const char **args);
void cmd_cd(int cnt, const char **args);
void cmd_drives(int cnt, const char **args);
void cmd_pci(int cnt, const char **args);
void cmd_mount(int cnt, const char **args);
void cmd_mkdir(int cnt, const char **args);
void cmd_rmdir(int cnt, const char **args);
void cmd_mkfile(int cnt, const char **args);
void cmd_rmfile(int cnt, const char **args);
void cmd_copy(int cnt, const char **args);
void cmd_run(int cnt, const char **args);
void cmd_exec(int cnt, const char **args);
void cmd_kill(int cnt, const char **args);
void cmd_sys(int cnt, const char **args);
void cmd_open(int cnt, const char **args);
void cmd_read_datetime(int cnt, const char **args);
void cmd_set_time(int cnt, const char **args);
void cmd_set_date(int cnt, const char **args);
void cmd_irq(int cnt, const char **args);
void cmd_sleep(int cnt, const char **args);
void cmd_exit(int cnt, const char **args);
void cmd_fdd(int cnt, const char **args);
void cmd_hdd(int cnt, const char **args);
void cmd_beep(int cnt, const char **args);
void cmd_wait(int cnt, const char **args);
void cmd_list_processes(int cnt, const char **args);
void cmd_start_task(int cnt, const char **args);
void cmd_net(int cnt, const char **args);
void cmd_ifconfig(int cnt, const char **args);
void cmd_ping(int cnt, const char **args);
void cmd_arp(int cnt, const char **args);
void cmd_history(int cnt, const char **args);
void cmd_get_ip(int cnt, const char **args);

// Command table
static const command_t command_table[MAX_COMMANDS] = {
    {"HELP", cmd_help},
    {"CLEAR", cmd_clear},
    {"CLS", cmd_cls},
    {"ECHO", cmd_echo},
    {"MEM", cmd_mem},
    {"DUMP", cmd_dump},
    {"DIR", cmd_ls},
    {"CD", cmd_cd},
    {"CHDIR", cmd_cd},
    {"DRIVES", cmd_drives},
    {"MOUNT", cmd_mount},
    {"MKDIR", cmd_mkdir},
    {"MD", cmd_mkdir},
    {"RMDIR", cmd_rmdir},
    {"RD", cmd_rmdir},
    {"MKFILE", cmd_mkfile},
    {"RMFILE", cmd_rmfile},
    {"DEL", cmd_rmfile},
    {"ERASE", cmd_rmfile},
    {"COPY", cmd_copy},
    {"RUN", cmd_run},
    {"EXEC", cmd_exec},
    {"KILL", cmd_kill},
    {"SYS", cmd_sys},
    {"OPEN", cmd_open},
    {"TYPE", cmd_open},
    {"DATETIME", cmd_read_datetime},
    {"SETTIME", cmd_set_time},
    {"SETDATE", cmd_set_date},
    {"IRQ", cmd_irq},
    {"SLEEP", cmd_sleep},
    {"EXIT", cmd_exit},
    {"QUIT", cmd_exit},
    {"FDD", cmd_fdd},
    {"HDD", cmd_hdd},
    {"BEEP", cmd_beep},
    {"WAIT", cmd_wait},
    {"PID", cmd_list_processes},
    {"RTASK", cmd_start_task},
    {"NET", cmd_net},
    {"IFCONFIG", cmd_ifconfig},
    {"PING", cmd_ping},
    {"ARP", cmd_arp},
    {"HISTORY", cmd_history},
    {"PCI", cmd_pci},
    {"GETIP", cmd_get_ip},
    {NULL, NULL} // End marker
};

#define MAX_ARGS 16
#define MAX_LENGTH MAX_LINE_LENGTH

typedef struct {
    drive_t* drive;
    char drive_path[SHELL_PATH_MAX];
    char vfs_path[SHELL_PATH_MAX];
    bool explicit_drive;
} shell_resolved_path_t;

static bool shell_native_drive_name(const char* value) {
    if (!value || strlen(value) != 4) return false;
    return ((tolower((unsigned char)value[0]) == 'h' &&
             tolower((unsigned char)value[1]) == 'd' &&
             tolower((unsigned char)value[2]) == 'd') ||
            (tolower((unsigned char)value[0]) == 'f' &&
             tolower((unsigned char)value[1]) == 'd' &&
             tolower((unsigned char)value[2]) == 'd')) &&
           value[3] >= '0' && value[3] <= '9';
}

static bool shell_drive_token(const char* value) {
    if (!value) return false;
    size_t length = strlen(value);
    if (length == 2 && isalpha((unsigned char)value[0]) &&
        value[1] == ':') return true;
    if (length == 5 && value[4] == ':') {
        char native[5];
        memcpy(native, value, 4);
        native[4] = '\0';
        return shell_native_drive_name(native);
    }
    return shell_native_drive_name(value);
}

static drive_t* shell_find_drive(const char* selector) {
    if (!selector || selector[0] == '\0') return current_drive;

    char drive_name[8];
    size_t length = strlen(selector);
    if (length == 1 && isalpha((unsigned char)selector[0])) {
        char letter = (char)toupper((unsigned char)selector[0]);
        if (letter == 'A' || letter == 'B') {
            snprintf(drive_name, sizeof(drive_name), "fdd%c",
                     (char)('0' + (letter - 'A')));
        } else if (letter >= 'C' && letter <= 'L') {
            snprintf(drive_name, sizeof(drive_name), "hdd%c",
                     (char)('0' + (letter - 'C')));
        } else {
            return NULL;
        }
    } else if (shell_native_drive_name(selector)) {
        for (size_t i = 0; i < 4; ++i) {
            drive_name[i] = (char)tolower((unsigned char)selector[i]);
        }
        drive_name[4] = '\0';
    } else {
        return NULL;
    }
    return get_drive_by_name(drive_name);
}

static int shell_drive_index(const drive_t* drive) {
    if (!drive || drive_count <= 0) return -1;
    int count = drive_count < MAX_DRIVES ? drive_count : MAX_DRIVES;
    for (int i = 0; i < count; i++) {
        if (&detected_drives[i] == drive) return i;
    }
    return -1;
}

static const char* shell_saved_drive_path(drive_t* drive) {
    int index = shell_drive_index(drive);
    if (index < 0) return "/";
    if (!shell_drive_path_initialized[index]) {
        strcpy(shell_drive_paths[index], "/");
        shell_drive_path_initialized[index] = true;
    }
    return shell_drive_paths[index];
}

static void shell_save_drive_path(drive_t* drive, const char* path) {
    int index = shell_drive_index(drive);
    if (index < 0 || !path) return;
    strncpy(shell_drive_paths[index], path, SHELL_PATH_MAX - 1);
    shell_drive_paths[index][SHELL_PATH_MAX - 1] = '\0';
    shell_drive_path_initialized[index] = true;
}

static void shell_drive_label(const drive_t* drive, char output[8]) {
    if (!drive || !output) return;
    if (strlen(drive->name) == 4 &&
        strncasecmp(drive->name, "hdd", 3) == 0 &&
        drive->name[3] >= '0' && drive->name[3] <= '9') {
        output[0] = (char)('C' + (drive->name[3] - '0'));
        output[1] = '\0';
        return;
    }
    if (strlen(drive->name) == 4 &&
        strncasecmp(drive->name, "fdd", 3) == 0 &&
        drive->name[3] >= '0' && drive->name[3] <= '9') {
        output[0] = (char)('A' + (drive->name[3] - '0'));
        output[1] = '\0';
        return;
    }
    strncpy(output, drive->name, 7);
    output[7] = '\0';
    str_to_upper(output);
}

static const char* shell_vfs_error(int result) {
    switch (result) {
        case VFS_ERR_NOT_FOUND: return "not found";
        case VFS_ERR_NO_MEMORY: return "not enough memory";
        case VFS_ERR_INVALID: return "invalid path or name";
        case VFS_ERR_IO: return "I/O error";
        case VFS_ERR_EXISTS: return "already exists";
        case VFS_ERR_NOT_DIR: return "not a directory";
        case VFS_ERR_IS_DIR: return "is a directory";
        case VFS_ERR_NO_SPACE: return "disk full";
        case VFS_ERR_READ_ONLY: return "read-only filesystem";
        case VFS_ERR_UNSUPPORTED: return "operation not supported";
        case VFS_ERR_BUSY: return "resource busy";
        default: return "filesystem error";
    }
}

static void shell_print_vfs_error(const char* operation, const char* path,
                                  int result) {
    printf("%s: %s", operation, shell_vfs_error(result));
    if (path && path[0] != '\0') printf(" - %s", path);
    printf("\n");
}

static bool shell_resolve_path(const char* input,
                               shell_resolved_path_t* resolved) {
    if (!input || !resolved) return false;

    char selector[SHELL_DRIVE_SELECTOR_MAX];
    const char* remainder = NULL;
    shell_path_result_t result = shell_path_split_drive(
        input, selector, &remainder);
    if (result != SHELL_PATH_OK) {
        printf("Invalid path: %s\n", input);
        return false;
    }

    resolved->explicit_drive = selector[0] != '\0';
    if (!resolved->explicit_drive && !current_drive) {
        printf("No active drive. Use DRIVES to list available drives.\n");
        return false;
    }
    resolved->drive = resolved->explicit_drive
        ? shell_find_drive(selector) : current_drive;
    if (!resolved->drive) {
        printf("Drive not found: %s\n", selector);
        return false;
    }
    if (resolved->drive->mount_point[0] == '\0') {
        printf("Drive is not mounted: %s\n", resolved->drive->name);
        return false;
    }

    const char* base = resolved->drive == current_drive
        ? current_path : shell_saved_drive_path(resolved->drive);
    result = shell_path_normalize(base, remainder, resolved->drive_path);
    if (result != SHELL_PATH_OK) {
        printf("Path is invalid or too long: %s\n", input);
        return false;
    }
    result = shell_path_join_mount(resolved->drive->mount_point,
                                   resolved->drive_path,
                                   resolved->vfs_path);
    if (result != SHELL_PATH_OK) {
        printf("Path is invalid or too long: %s\n", input);
        return false;
    }
    return true;
}

static bool try_run_program_without_extension(const char *name, int arg_count,
                                              char *const *arguments) {
    char program_name[MAX_LENGTH];
    size_t length;
    shell_resolved_path_t resolved;
    vfs_node_t *node = NULL;

    if (name == NULL || is_program_filename(name)) return false;
    length = strlen(name);
    if (length > sizeof(program_name) - sizeof(".PRG")) return false;

    strcpy(program_name, name);
    strcpy(program_name + length, ".PRG");
    if (!shell_resolve_path(program_name, &resolved)) return false;
    if (vfs_open(resolved.vfs_path, &node) != VFS_OK || node == NULL) {
        return false;
    }
    bool executable_file = node->type == VFS_FILE;
    (void)vfs_close(node);
    if (!executable_file) return false;

    const char *program_arguments[MAX_ARGS + 1];
    program_arguments[0] = program_name;
    for (int i = 0; i < arg_count; ++i) program_arguments[i + 1] = arguments[i];
    cmd_run(arg_count + 1, program_arguments);
    return true;
}

static void shell_restore_drive(drive_t* saved_drive) {
    current_drive = saved_drive;
}

static bool shell_path_is_active_or_ancestor(
    const shell_resolved_path_t* resolved) {
    if (!resolved || resolved->drive != current_drive) return false;
    size_t length = strlen(resolved->drive_path);
    size_t current_length = strlen(current_path);
    if (length == 0 || length > current_length ||
        strncasecmp(resolved->drive_path, current_path, length) != 0) {
        return false;
    }
    return length == 1 || length == current_length ||
           current_path[length] == '/';
}

bool try_switch_drive(const char* name) {
    if (!shell_drive_token(name)) return false;

    char selector[SHELL_DRIVE_SELECTOR_MAX];
    size_t length = strlen(name);
    if (length > 0 && name[length - 1] == ':') --length;
    if (length >= sizeof(selector)) return false;
    memcpy(selector, name, length);
    selector[length] = '\0';

    drive_t* drive = shell_find_drive(selector);
    if (!drive) {
        printf("Drive not found: %s\n", name);
        return false;
    }
    if (drive->mount_point[0] == '\0') {
        printf("Drive is not mounted: %s\n", drive->name);
        return false;
    }
    shell_save_drive_path(current_drive, current_path);
    current_drive = drive;
    strcpy(current_path, shell_saved_drive_path(drive));
    return true;
}

void process_command(char *input_buffer) {
    char command[MAX_LENGTH];
    char original_command[MAX_LENGTH];
    char* arguments[MAX_ARGS] = {NULL};  // Initialize all pointers to NULL
    int arg_cnt = split_input(input_buffer, command, arguments, MAX_LENGTH, MAX_ARGS);

    // split_input already prints the specific syntax/allocation error.
    if (arg_cnt < 0) {
        return;
    }

    // Check if the command is empty
    if (command[0] == '\0') {
        free_arguments(arguments, arg_cnt);  // Free allocated arguments
        return; // Empty input
    }
    
    // A DOS/native drive token is a complete command (C:, hdd0:, hdd0).
    if (shell_drive_token(command)) {
        if (arg_cnt != 0) {
            printf("Syntax error: a drive change takes no arguments.\n");
        } else {
            (void)try_switch_drive(command);
        }
        free_arguments(arguments, arg_cnt);
        return;
    }

    // Preserve a filename's spelling for case-sensitive filesystems before
    // normalizing built-in command names.
    strcpy(original_command, command);
    str_to_upper(command);

    // Match command
    int found = 0;
    for (int i = 0; command_table[i].name != NULL; i++) {
        if (strcmp(command, command_table[i].name) == 0) {
            command_table[i].execute(arg_cnt, (const char**)arguments);
            found = 1;
            break;
        }
    }

    if (!found) {
        if (is_program_filename(original_command)) {
            const char *program_arguments[MAX_ARGS + 1];
            program_arguments[0] = original_command;
            for (int i = 0; i < arg_cnt; ++i) {
                program_arguments[i + 1] = arguments[i];
            }
            cmd_run(arg_cnt + 1, program_arguments);
        } else if (!try_run_program_without_extension(original_command,
                                                      arg_cnt, arguments)) {
            printf("Unknown command: %s\n", command);
        }
    }

    // Free allocated arguments after command execution
    free_arguments(arguments, arg_cnt);

    // Don't trigger timer interrupts manually in single-threaded mode
    // asm volatile("int $0x29"); // Trigger a timer interrupt
}

//=============================================================================
// COMMAND HISTORY
//=============================================================================

#define HISTORY_SIZE 50
#define HISTORY_LINE_MAX 256

static char history_buffer[HISTORY_SIZE][HISTORY_LINE_MAX];
static int history_count = 0;
static int history_index = 0;
static int history_current = -1;  // -1 means not browsing history
static char history_draft[HISTORY_LINE_MAX];

/**
 * Add command to history (avoid duplicates of last command)
 */
void history_add(const char *cmd) {
    // Don't add empty commands
    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }
    
    // Don't add if same as last command
    if (history_count > 0) {
        int last_idx = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        if (strcmp(history_buffer[last_idx], cmd) == 0) {
            return;
        }
    }
    
    // Add to history
    strncpy(history_buffer[history_index], cmd, HISTORY_LINE_MAX - 1);
    history_buffer[history_index][HISTORY_LINE_MAX - 1] = '\0';
    
    history_index = (history_index + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
    
    // Reset browsing position
    history_current = -1;
}

/**
 * Get previous command in history
 */
const char* history_get_prev(void) {
    if (history_count == 0) {
        return NULL;
    }
    
    if (history_current == -1) {
        // Start from most recent
        history_current = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    } else {
        // Go back one more
        int prev = (history_current - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        
        // Check if we've reached the oldest command
        int oldest = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
        if (history_current == oldest) {
            return NULL;  // Already at oldest
        }
        
        history_current = prev;
    }
    
    return history_buffer[history_current];
}

/**
 * Get next command in history (towards newer)
 */
const char* history_get_next(void) {
    if (history_current == -1 || history_count == 0) {
        return NULL;  // Not browsing or no history
    }
    
    int newest = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;
    if (history_current == newest) {
        // At newest, restore the line that existed before history browsing.
        history_current = -1;
        return history_draft;
    }
    
    history_current = (history_current + 1) % HISTORY_SIZE;
    return history_buffer[history_current];
}

/**
 * Reset history browsing to newest
 */
void history_reset(void) {
    history_current = -1;
    history_draft[0] = '\0';
}

/**
 * List all commands in history
 */
void history_list(void) {
    if (history_count == 0) {
        printf("No command history.\n");
        return;
    }
    
    printf("Command History (%d commands):\n", history_count);
    
    int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % HISTORY_SIZE;
        printf("  %3d: %s\n", i + 1, history_buffer[idx]);
    }
}

//=============================================================================
// LINE EDITOR
//=============================================================================

/**
 * Replace current line with new content
 */
static void replace_current_line(char *buffer, const char *new_content, int *cursor_pos, int *buffer_index) {
    // Save cursor position
    vga_save_cursor();
    
    // Clear current line
    vga_clear_line();
    
    // Print prompt
    show_prompt();
    
    // Copy new content
    int len = strlen(new_content);
    if (len >= MAX_LINE_LENGTH) len = MAX_LINE_LENGTH - 1;
    
    strncpy(buffer, new_content, len);
    buffer[len] = '\0';
    
    // Display new content
    for (int i = 0; i < len; i++) {
        putchar(buffer[i]);
    }
    
    *cursor_pos = len;
    *buffer_index = len;
}

/**
 * Apply an already decoded ANSI cursor/history key.  Decoding is deliberately
 * stateful in command_loop(): bytes from a real serial terminal do not arrive
 * atomically, unlike the ANSI sequences queued by the PS/2 driver.
 */
static bool handle_escape_key(char key_code, char *buffer,
                              int *buffer_index, int *cursor_pos) {
    switch (key_code) {
        case 'A': {
            // Previous command in history
            if (history_current == -1) {
                strncpy(history_draft, buffer, sizeof(history_draft) - 1);
                history_draft[sizeof(history_draft) - 1] = '\0';
            }
            const char *prev = history_get_prev();
            if (prev != NULL) {
                replace_current_line(buffer, prev, cursor_pos, buffer_index);
            }
            return true;
        }
        
        case 'B': {
            // Next command in history
            const char *next = history_get_next();
            if (next != NULL) {
                replace_current_line(buffer, next, cursor_pos, buffer_index);
            }
            return true;
        }
        
        case 'D':
            // Move cursor left within line
            if (*cursor_pos > 0) {
                (*cursor_pos)--;
                vga_move_cursor_left();
            }
            return true;
            
        case 'C':
            // Move cursor right within line
            if (*cursor_pos < *buffer_index) {
                (*cursor_pos)++;
                vga_move_cursor_right();
            }
            return true;
            
        case 'H':
            // Jump to start of line (after prompt)
            while (*cursor_pos > 0) {
                (*cursor_pos)--;
                vga_move_cursor_left();
            }
            return true;
            
        case 'F':
            // Jump to end of line
            while (*cursor_pos < *buffer_index) {
                (*cursor_pos)++;
                vga_move_cursor_right();
            }
            return true;
            
        case '3':
            // Delete character at cursor
            if (*cursor_pos < *buffer_index) {
                // Shift characters left
                for (int i = *cursor_pos; i < *buffer_index - 1; i++) {
                    buffer[i] = buffer[i + 1];
                }
                (*buffer_index)--;
                buffer[*buffer_index] = '\0';
                
                // Update display
                for (int i = *cursor_pos; i < *buffer_index; i++) {
                    putchar(buffer[i]);
                }
                putchar(' ');  // Clear last character
                
                // Move cursor back to position
                int moves_back = *buffer_index - *cursor_pos + 1;
                for (int i = 0; i < moves_back; i++) {
                    vga_move_cursor_left();
                }
            }
            return true;
            
        default:
            return false;
    }
}

/**
 * Handle Ctrl+key combinations
 */
static bool handle_ctrl_key(char ch, char *buffer, int *buffer_index, int *cursor_pos) {
    switch (ch) {
        case 0x03:  // Ctrl+C
            printf("^C\n");
            buffer[0] = '\0';
            *buffer_index = 0;
            *cursor_pos = 0;
            history_reset();
            show_prompt();
            return true;
            
        case 0x04:  // Ctrl+D (EOF)
            if (*buffer_index == 0) {
                printf("^D\n(Ctrl+D pressed - use 'exit' to quit)\n");
                show_prompt();
                return true;
            }
            return false;
            
        case 0x0C:  // Ctrl+L (clear screen)
            clear_screen();
            show_prompt();
            // Redisplay current buffer
            for (int i = 0; i < *buffer_index; i++) {
                putchar(buffer[i]);
            }
            // Restore cursor position
            int moves_back = *buffer_index - *cursor_pos;
            for (int i = 0; i < moves_back; i++) {
                vga_move_cursor_left();
            }
            return true;
            
        case 0x15:  // Ctrl+U (clear line)
            vga_clear_line();
            show_prompt();
            buffer[0] = '\0';
            *buffer_index = 0;
            *cursor_pos = 0;
            return true;
            
        case 0x0B:  // Ctrl+K (kill to end of line)
            // Kill from cursor to end
            buffer[*cursor_pos] = '\0';
            *buffer_index = *cursor_pos;
            vga_clear_from_cursor();
            return true;
            
        default:
            return false;
    }
}

//=============================================================================
// ENHANCED COMMAND LOOP
//=============================================================================

/**
 * Display shell prompt with current drive name
 */
void show_prompt(void) {
    extern drive_t* current_drive;

    if (current_drive && current_drive->name[0]) {
        char drive_label[8];
        char dos_path[SHELL_PATH_MAX];
        shell_drive_label(current_drive, drive_label);
        if (shell_path_to_dos(current_path, dos_path) != SHELL_PATH_OK) {
            strcpy(dos_path, "\\");
        }
        printf("%s:%s>", drive_label, dos_path);
    } else {
        printf(">");
    }
}

typedef enum {
    SHELL_ESCAPE_GROUND = 0,
    SHELL_ESCAPE_SEEN,
    SHELL_ESCAPE_CSI,
    SHELL_ESCAPE_SS3
} shell_escape_state_t;

void command_loop(void) {
    show_prompt();

    char* input = (char*)k_malloc(MAX_LINE_LENGTH);
    if (input == NULL) {
        printf("Failed to allocate memory for input buffer\n");
        return;
    }

    int buffer_index = 0;  // End of text in buffer
    int cursor_pos = 0;     // Current cursor position (0 to buffer_index)
    shell_escape_state_t escape_state = SHELL_ESCAPE_GROUND;
    unsigned int escape_parameter = 0;
    unsigned int escape_digits = 0;
    unsigned int escape_length = 0;
    input[0] = '\0';

    while (1) {
        /* Drain pending keyboard/COM1 input before doing network work.  This
         * keeps the IRQ-backed input queues responsive even when a network
         * backend needs a comparatively expensive polling pass. */
        char ch = getchar_nonblocking();
        if (ch == 0) {
            fdd_service();
            netdev_poll();
            /* Network processing may take long enough for input to arrive. */
            ch = getchar_nonblocking();
        }
        
        if (ch != 0) {
process_input_byte:
            /* ANSI sequences must be decoded across loop iterations.  Serial
             * terminals commonly deliver ESC, '[', and the final byte in
             * separate UART interrupts. */
            if (escape_state == SHELL_ESCAPE_SEEN) {
                if (ch == '[') {
                    escape_state = SHELL_ESCAPE_CSI;
                    escape_parameter = 0;
                    escape_digits = 0;
                    escape_length = 0;
                    continue;
                }
                if (ch == 'O') {
                    escape_state = SHELL_ESCAPE_SS3;
                    escape_length = 0;
                    continue;
                }
                escape_state = SHELL_ESCAPE_GROUND;
                goto process_input_byte;
            }
            if (escape_state == SHELL_ESCAPE_SS3) {
                escape_state = SHELL_ESCAPE_GROUND;
                if (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D' ||
                    ch == 'H' || ch == 'F') {
                    (void)handle_escape_key(ch, input, &buffer_index,
                                            &cursor_pos);
                    continue;
                }
                goto process_input_byte;
            }
            if (escape_state == SHELL_ESCAPE_CSI) {
                escape_length++;
                if (escape_length > 8) {
                    escape_state = SHELL_ESCAPE_GROUND;
                    goto process_input_byte;
                }
                if (ch >= '0' && ch <= '9') {
                    if (escape_digits >= 3) {
                        escape_state = SHELL_ESCAPE_GROUND;
                        goto process_input_byte;
                    }
                    escape_parameter = escape_parameter * 10U +
                                       (unsigned int)(ch - '0');
                    escape_digits++;
                    continue;
                }
                if (ch == '~' && escape_digits != 0) {
                    char decoded_key = '\0';
                    if (escape_parameter == 1 || escape_parameter == 7) {
                        decoded_key = 'H';
                    } else if (escape_parameter == 3) {
                        decoded_key = '3';
                    } else if (escape_parameter == 4 || escape_parameter == 8) {
                        decoded_key = 'F';
                    }
                    escape_state = SHELL_ESCAPE_GROUND;
                    if (decoded_key != '\0') {
                        (void)handle_escape_key(decoded_key, input,
                                                &buffer_index, &cursor_pos);
                    }
                    continue;
                }
                if (escape_digits == 0 &&
                    (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D' ||
                     ch == 'H' || ch == 'F')) {
                    escape_state = SHELL_ESCAPE_GROUND;
                    (void)handle_escape_key(ch, input, &buffer_index,
                                            &cursor_pos);
                    continue;
                }
                escape_state = SHELL_ESCAPE_GROUND;
                goto process_input_byte;
            }

            // Start an ANSI/VT escape sequence.
            if (ch == '\x1B') {
                escape_state = SHELL_ESCAPE_SEEN;
                continue;
            }
            // Check for Ctrl+key combinations
            else if (ch < 0x20 && ch != '\n' && ch != '\t' && ch != '\b') {
                if (handle_ctrl_key(ch, input, &buffer_index, &cursor_pos)) {
                    escape_state = SHELL_ESCAPE_GROUND;
                    continue;
                }
            }
            // Handle Enter key
            else if (ch == '\n') {
                input[buffer_index] = '\0';
                printf("\n");
                
                // Add to history if not empty
                if (buffer_index > 0) {
                    history_add(input);
                    process_command(input);
                }
                
                // Reset for next command
                buffer_index = 0;
                cursor_pos = 0;
                input[0] = '\0';
                history_reset();
                escape_state = SHELL_ESCAPE_GROUND;
                show_prompt();
            }
            // Handle Backspace
            else if (ch == '\b') {
                if (cursor_pos > 0) {
                    // Delete character before cursor
                    for (int i = cursor_pos - 1; i < buffer_index; i++) {
                        input[i] = input[i + 1];
                    }
                    buffer_index--;
                    cursor_pos--;
                    input[buffer_index] = '\0';
                    
                    // Update display: move cursor left, reprint from cursor, clear last char
                    vga_backspace();
                    for (int i = cursor_pos; i < buffer_index; i++) {
                        putchar(input[i]);
                    }
                    putchar(' ');  // Clear last character
                    
                    // Move cursor back to correct position
                    int moves_back = buffer_index - cursor_pos + 1;
                    for (int i = 0; i < moves_back; i++) {
                        vga_move_cursor_left();
                    }
                }
            }
            // Handle Tab (could be used for autocomplete later)
            else if (ch == '\t') {
                // For now, ignore or insert spaces
                // TODO: Implement command/filename autocomplete
            }
            // Regular character
            else if (ch >= 0x20 && ch < 0x7F) {
                if (buffer_index < MAX_LINE_LENGTH - 1) {
                    // Insert character at cursor position
                    if (cursor_pos < buffer_index) {
                        // Shift characters right
                        for (int i = buffer_index; i > cursor_pos; i--) {
                            input[i] = input[i - 1];
                        }
                    }
                    
                    input[cursor_pos] = ch;
                    buffer_index++;
                    cursor_pos++;
                    input[buffer_index] = '\0';
                    
                    // Update display
                    if (cursor_pos == buffer_index) {
                        // Cursor at end, simple append
                        putchar(ch);
                    } else {
                        // Mid-line insert: reprint from cursor
                        for (int i = cursor_pos - 1; i < buffer_index; i++) {
                            putchar(input[i]);
                        }
                        // Move cursor back to correct position
                        int moves_back = buffer_index - cursor_pos;
                        for (int i = 0; i < moves_back; i++) {
                            vga_move_cursor_left();
                        }
                    }
                }
            }
        } else {
            // No input available - use HLT to wait efficiently
            __asm__ __volatile__("hlt");
        }
    }
}

// Splits an input string into a command and arguments
int split_input(const char* input, char* command, char** arguments,
                int max_length, int max_args) {
    int i = 0;
    int j = 0;
    int arg_count = 0;
    if (!input || !command || !arguments || max_length < 2 || max_args < 1) {
        return -1;
    }

    while (input[i] != '\0' && isspace((unsigned char)input[i])) i++;
    while (input[i] != '\0' && !isspace((unsigned char)input[i])) {
        if (j >= max_length - 1) {
            printf("Command name is too long.\n");
            return -1;
        }
        command[j++] = input[i++];
    }
    command[j] = '\0';

    while (input[i] != '\0') {
        while (input[i] != '\0' && isspace((unsigned char)input[i])) i++;
        if (input[i] == '\0') break;
        if (arg_count >= max_args) {
            printf("Too many command arguments (maximum %d).\n", max_args);
            free_arguments(arguments, arg_count);
            return -1;
        }

        arguments[arg_count] = (char*)malloc(MAX_LINE_LENGTH);
        if (!arguments[arg_count]) {
            printf("Not enough memory to parse the command.\n");
            free_arguments(arguments, arg_count);
            return -1;
        }

        bool quoted = false;
        j = 0;
        while (input[i] != '\0') {
            char value = input[i];
            if (value == '"') {
                quoted = !quoted;
                i++;
                continue;
            }
            if (!quoted && isspace((unsigned char)value)) break;
            if (j >= MAX_LINE_LENGTH - 1) {
                printf("Command argument is too long.\n");
                free(arguments[arg_count]);
                arguments[arg_count] = NULL;
                free_arguments(arguments, arg_count);
                return -1;
            }
            arguments[arg_count][j++] = value;
            i++;
        }
        if (quoted) {
            printf("Missing closing quote.\n");
            free(arguments[arg_count]);
            arguments[arg_count] = NULL;
            free_arguments(arguments, arg_count);
            return -1;
        }
        arguments[arg_count][j] = '\0';
        arg_count++;
    }
    return arg_count;
}

// Free arguments array
void free_arguments(char** arguments, int arg_count) {
    if (arguments == NULL) return;
    
    for (int i = 0; i < arg_count; i++) {
        if (arguments[i] != NULL) {
            free(arguments[i]);
            arguments[i] = NULL;
        }
    }
}

// Command implementations
void cmd_help(int arg_count, const char **args) {
    (void)arg_count;
    (void)args;
    printf("\nDOS-compatible file commands:\n");
    printf("  DIR [path]       List files and directories\n");
    printf("  CD [path]        Show or change the current directory\n");
    printf("  TYPE <file>      Display a text file (alias: OPEN)\n");
    printf("  MD <directory>   Create a directory (alias: MKDIR)\n");
    printf("  RD <directory>   Remove a directory (alias: RMDIR)\n");
    printf("  DEL <file>       Delete a file (aliases: ERASE, RMFILE)\n");
    printf("  COPY <src> <dst> Copy a file; existing destinations are protected\n");
    printf("  MKFILE <file>    Create an empty file\n");
    printf("  CLS              Clear the screen\n");
    printf("  ECHO [text]      Display text\n");
    printf("  C: / HDD0:       Change drive\n");
    printf("\nSystem commands:\n");
    printf("  DRIVES  MOUNT  MEM  DUMP  PCI  IRQ  DATETIME\n");
    printf("  RUN     EXEC   PID  KILL  BASIC  HISTORY\n");
    printf("  NET     IFCONFIG  PING  ARP  GETIP\n");
    printf("\nPaths accept both \\ and /, plus the . and .. components.\n");
    printf("Use arrow keys for history/editing and Ctrl+L to clear.\n\n");
}

void cmd_clear(int arg_count, const char **args) {
    clear_screen();
}

void cmd_echo(int arg_count, const char **args) {
    if(arg_count == 0) {
        printf("ECHO is on.\n");
    } else {
        for (int i = 0; i < arg_count; i++) {
            if (i != 0) putchar(' ');
            printf("%s", args[i]);
        }
        printf("\n");
    }
}

void cmd_mem(int arg_count, const char **args) {
    // print the current memory map
    // get the memory map from the bootloader
    //print_memory_map(sys_mb_info);

    printf("Enter a value: ");
    int intput = getchar();
    printf("You entered: %c\n", intput);
}

void cmd_dump(int arg_count, const char** arguments) {
    uint32_t start_address = 0x80000000, end_address = 0x80000100;
    if (arg_count > 0) start_address = (uint32_t)strtoul(arguments[0], NULL, 16);
    if (arg_count > 1) end_address = (uint32_t)strtoul(arguments[1], NULL, 16);
    memory_dump(start_address, end_address);
}

void cmd_cls(int arg_count, const char** arguments) {
    clear_screen();
}

void cmd_drives(int arg_count, const char** arguments) {
    (void)arg_count;
    (void)arguments;
    if (drive_count <= 0) {
        printf("No drives detected.\n");
        return;
    }

    printf("\nDrive  Device  Type   Mount point\n");
    printf("---------------------------------------------\n");
    for (int i = 0; i < drive_count; i++) {
        drive_t* drive = &detected_drives[i];
        char label[8];
        shell_drive_label(drive, label);
        const char* type = drive->type == DRIVE_TYPE_ATA ? "HDD" :
                           drive->type == DRIVE_TYPE_FDD ? "FDD" : "?";
        const char* mount = drive->mount_point[0] != '\0'
            ? drive->mount_point : "(not mounted)";
        printf("%c%-5s %-7s %-6s %s\n",
               drive == current_drive ? '*' : ' ', label,
               drive->name, type, mount);
    }
    printf("\n* current drive\n");
}

/// @brief Mount an attached drive
/// @param arg_count 
/// @param arguments 
void cmd_mount(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: MOUNT <drive>\n");
        cmd_drives(0, NULL);
        return;
    }

    char selector[SHELL_DRIVE_SELECTOR_MAX];
    size_t length = strlen(arguments[0]);
    if (length > 0 && arguments[0][length - 1] == ':') length--;
    if (length == 0 || length >= sizeof(selector)) {
        printf("MOUNT: invalid drive - %s\n", arguments[0]);
        return;
    }
    memcpy(selector, arguments[0], length);
    selector[length] = '\0';

    drive_t* target = shell_find_drive(selector);
    if (!target) {
        printf("MOUNT: drive not found - %s\n", arguments[0]);
        return;
    }
    if (target->mount_point[0] != '\0') {
        printf("Drive %s is already mounted at %s.\n",
               target->name, target->mount_point);
        return;
    }

    const char* fs_type = target->type == DRIVE_TYPE_FDD ? "fat12" : "fat32";
    if (target->type == DRIVE_TYPE_ATA) {
        uint8_t ext2_buffer[1024];
        if (ata_read_sector(target->base, 2, ext2_buffer,
                            target->is_master) &&
            ata_read_sector(target->base, 3, ext2_buffer + 512,
                            target->is_master) &&
            *(uint16_t*)(ext2_buffer + 56) == 0xEF53) {
            fs_type = "ext2";
        }
    } else if (target->type != DRIVE_TYPE_FDD) {
        printf("MOUNT: unsupported drive type - %s\n", target->name);
        return;
    }

    char mount_path[64];
    snprintf(mount_path, sizeof(mount_path), "/mnt/%s", target->name);
    drive_t* saved_drive = current_drive;
    int result = vfs_mount(target, fs_type, mount_path);
    current_drive = saved_drive;
    if (result != VFS_OK) {
        shell_print_vfs_error("MOUNT", arguments[0], result);
        return;
    }

    strncpy(target->mount_point, mount_path, sizeof(target->mount_point) - 1);
    target->mount_point[sizeof(target->mount_point) - 1] = '\0';
    printf("Mounted %s at %s (%s).\n", target->name, mount_path, fs_type);
}

/// @brief List the directory content
/// @param arg_count 
/// @param arguments 
void cmd_ls(int arg_count, const char** arguments) {
    if (arg_count > 1) {
        printf("Usage: DIR [path]\n");
        return;
    }
    const char* requested = arg_count == 0 ? "" : arguments[0];
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(requested, &resolved)) return;

    drive_t* saved_drive = current_drive;
    vfs_dir_entry_t directory_stat;
    int result = vfs_stat(resolved.vfs_path, &directory_stat);
    if (result != VFS_OK) {
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("DIR", requested, result);
        return;
    }

    char drive_label[8];
    char dos_path[SHELL_PATH_MAX];
    shell_drive_label(resolved.drive, drive_label);
    (void)shell_path_to_dos(resolved.drive_path, dos_path);

    printf("\n Volume in drive %s is %s\n", drive_label,
           resolved.drive->name);
    printf(" Directory of %s:%s\n\n", drive_label, dos_path);
    printf("%-40s %12s\n", "NAME", "SIZE");
    printf("-----------------------------------------------------\n");

    uint32_t file_count = 0;
    uint32_t directory_count = 0;
    uint64_t total_bytes = 0;

    if (directory_stat.type != VFS_DIRECTORY) {
        const char* type = directory_stat.type == VFS_FILE ? "" : "<OTHER>";
        printf("%-40s %12u %s\n", directory_stat.name,
               directory_stat.size, type);
        if (directory_stat.type == VFS_FILE) {
            file_count = 1;
            total_bytes = directory_stat.size;
        }
    } else {
        uint32_t index = 0;
        vfs_dir_entry_t entry;
        while ((result = vfs_readdir(resolved.vfs_path, index, &entry)) ==
               VFS_OK) {
            if (entry.type == VFS_DIRECTORY) {
                printf("%-40s %12s\n", entry.name, "<DIR>");
                directory_count++;
            } else {
                printf("%-40s %12u\n", entry.name, entry.size);
                file_count++;
                total_bytes += entry.size;
            }
            index++;
        }
        if (result != VFS_ERR_NOT_FOUND) {
            shell_restore_drive(saved_drive);
            shell_print_vfs_error("DIR", requested, result);
            return;
        }
    }

    shell_restore_drive(saved_drive);
    printf("%10u File(s) %llu bytes\n", file_count,
           (unsigned long long)total_bytes);
    printf("%10u Dir(s)\n\n", directory_count);
}

void cmd_cd(int arg_count, const char** arguments) {
    if (arg_count > 1) {
        printf("Usage: CD [path]\n");
        return;
    }
    if (arg_count == 0) {
        if (!current_drive) {
            printf("No active drive.\n");
            return;
        }
        char drive_label[8];
        char dos_path[SHELL_PATH_MAX];
        shell_drive_label(current_drive, drive_label);
        (void)shell_path_to_dos(current_path, dos_path);
        printf("%s:%s\n", drive_label, dos_path);
        return;
    }

    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;

    drive_t* saved_drive = current_drive;
    vfs_dir_entry_t entry;
    int result = vfs_stat(resolved.vfs_path, &entry);
    if (result != VFS_OK) {
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("CD", arguments[0], result);
        return;
    }
    if (entry.type != VFS_DIRECTORY) {
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("CD", arguments[0], VFS_ERR_NOT_DIR);
        return;
    }

    shell_save_drive_path(current_drive, current_path);
    current_drive = resolved.drive;
    strcpy(current_path, resolved.drive_path);
    shell_save_drive_path(current_drive, current_path);
}

void cmd_mkdir(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: MD <directory>\n");
        return;
    }
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;
    drive_t* saved_drive = current_drive;
    int result = vfs_mkdir(resolved.vfs_path);
    shell_restore_drive(saved_drive);
    if (result != VFS_OK) shell_print_vfs_error("MD", arguments[0], result);
}

void cmd_rmdir(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: RD <directory>\n");
        return;
    }
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;
    if (shell_path_is_active_or_ancestor(&resolved)) {
        printf("RD: cannot remove the current directory or its parent.\n");
        return;
    }
    drive_t* saved_drive = current_drive;
    int result = vfs_rmdir(resolved.vfs_path);
    shell_restore_drive(saved_drive);
    if (result != VFS_OK) shell_print_vfs_error("RD", arguments[0], result);
}

void cmd_mkfile(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: MKFILE <file>\n");
        return;
    }
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;
    drive_t* saved_drive = current_drive;
    int result = vfs_create(resolved.vfs_path);
    shell_restore_drive(saved_drive);
    if (result != VFS_OK) {
        shell_print_vfs_error("MKFILE", arguments[0], result);
    }
}

void cmd_rmfile(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: DEL <file>\n");
        return;
    }
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;
    drive_t* saved_drive = current_drive;
    int result = vfs_delete(resolved.vfs_path);
    shell_restore_drive(saved_drive);
    if (result != VFS_OK) shell_print_vfs_error("DEL", arguments[0], result);
}

void cmd_copy(int arg_count, const char** arguments) {
    if (arg_count != 2) {
        printf("Usage: COPY <source> <destination>\n");
        return;
    }

    shell_resolved_path_t source;
    shell_resolved_path_t destination;
    if (!shell_resolve_path(arguments[0], &source) ||
        !shell_resolve_path(arguments[1], &destination)) {
        return;
    }

    drive_t* saved_drive = current_drive;
    vfs_node_t* source_node = NULL;
    vfs_node_t* destination_node = NULL;
    bool destination_created = false;
    int result = vfs_open(source.vfs_path, &source_node);
    if (result != VFS_OK || !source_node) {
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("COPY", arguments[0], result);
        return;
    }
    if (source_node->type != VFS_FILE) {
        (void)vfs_close(source_node);
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("COPY", arguments[0], VFS_ERR_IS_DIR);
        return;
    }

    vfs_dir_entry_t destination_stat;
    result = vfs_stat(destination.vfs_path, &destination_stat);
    if (result == VFS_OK && destination_stat.type == VFS_DIRECTORY) {
        char appended[SHELL_PATH_MAX];
        if (shell_path_normalize(destination.drive_path, source_node->name,
                                 appended) != SHELL_PATH_OK ||
            shell_path_join_mount(destination.drive->mount_point, appended,
                                  destination.vfs_path) != SHELL_PATH_OK) {
            (void)vfs_close(source_node);
            shell_restore_drive(saved_drive);
            printf("COPY: destination path is too long.\n");
            return;
        }
        strcpy(destination.drive_path, appended);
        result = vfs_stat(destination.vfs_path, &destination_stat);
    }
    if (result == VFS_OK) {
        (void)vfs_close(source_node);
        shell_restore_drive(saved_drive);
        printf("COPY: destination already exists - %s\n", arguments[1]);
        return;
    }
    if (result != VFS_ERR_NOT_FOUND) {
        (void)vfs_close(source_node);
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("COPY", arguments[1], result);
        return;
    }
    if (strcmp(source.vfs_path, destination.vfs_path) == 0) {
        (void)vfs_close(source_node);
        shell_restore_drive(saved_drive);
        printf("COPY: source and destination are the same file.\n");
        return;
    }

    result = vfs_create(destination.vfs_path);
    if (result != VFS_OK) goto copy_failed;
    destination_created = true;
    result = vfs_open(destination.vfs_path, &destination_node);
    if (result != VFS_OK || !destination_node) goto copy_failed;

    uint8_t buffer[1024];
    uint32_t source_offset = 0;
    uint32_t destination_offset = 0;
    while (source_offset < source_node->size) {
        uint32_t amount = source_node->size - source_offset;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        int read = vfs_read(source_node, source_offset, amount, buffer);
        if (read <= 0 || (uint32_t)read > amount) {
            result = read < 0 ? read : VFS_ERR_IO;
            goto copy_failed;
        }
        uint32_t written_total = 0;
        while (written_total < (uint32_t)read) {
            int written = vfs_write(destination_node,
                                    destination_offset + written_total,
                                    (uint32_t)read - written_total,
                                    buffer + written_total);
            if (written <= 0 ||
                (uint32_t)written > (uint32_t)read - written_total) {
                result = written < 0 ? written : VFS_ERR_IO;
                goto copy_failed;
            }
            written_total += (uint32_t)written;
        }
        source_offset += (uint32_t)read;
        destination_offset += (uint32_t)read;
    }

    (void)vfs_close(destination_node);
    (void)vfs_close(source_node);
    shell_restore_drive(saved_drive);
    printf("        1 file(s) copied.\n");
    return;

copy_failed:
    if (destination_node) (void)vfs_close(destination_node);
    if (source_node) (void)vfs_close(source_node);
    if (destination_created) (void)vfs_delete(destination.vfs_path);
    shell_restore_drive(saved_drive);
    shell_print_vfs_error("COPY", arguments[1], result);
}

void cmd_exec(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: EXEC <program.prg>\n");
        return;
    }
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;
    drive_t* saved_drive = current_drive;
    int pid = create_process_for_file(resolved.vfs_path);
    shell_restore_drive(saved_drive);
    if (pid < 0) printf("Failed to start '%s'.\n", arguments[0]);
}

void cmd_kill(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("KILL command without arguments\n");
    } else {
        int pid = strtoul(arguments[0], NULL, 10);
        terminate_process(pid);
    }
}

//TryContext ctx;

void cmd_sys(int arg_count, const char** arguments) {
    // if (arg_count == 0) {
    //     printf("SYS command without arguments\n");
    // } else {
    //     long entryPoint = strtoul(arguments[0], NULL, 16);
    //     start_program_execution(entryPoint);
    // }

    //current_try_context = &ctx; // Set the global context pointer

    // printf("Set context in user program: 0x%p\n", (void*)current_try_context);
    // printf("Current ESP: 0x%X, ", get_esp());
    // printf("EBP: 0x%X\n", get_ebp());

    // if (setjmp(&ctx) == 0) {
    //     int x = 10;
    //     int y = 0;
    //     int z = x / y; // Trigger divide-by-zero exception
    //     printf("z = %d\n", z);
    //     printf("Try block executed successfully\n");
    // } else {
    //     printf("Caught divide-by-zero exception\n");
    // }

    // current_try_context = NULL; // Clear the context pointer
    printf("Program execution continues...\n");
}

void cmd_open(int arg_count, const char** arguments) {
    if (arg_count != 1) {
        printf("Usage: TYPE <file>\n");
    } else {
        open_file(arguments[0]);
    }
}

void cmd_read_datetime(int arg_count, const char** arguments) {
    int hour, minute, second, year, month, day;
    read_time(&hour, &minute, &second);
    read_date(&year, &month, &day);

    printf("Current date and time: %d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
}

void cmd_set_time(int arg_count, const char** arguments) {
    if (arg_count < 3) {
        printf("SETTIME command requires hour, minute, and second\n");
    } else {
        int hour = strtoul(arguments[0], NULL, 10);
        int minute = strtoul(arguments[1], NULL, 10);
        int second = strtoul(arguments[2], NULL, 10);
        write_time(hour, minute, second);
    }
}

void cmd_set_date(int arg_count, const char** arguments) {
    if (arg_count < 3) {
        printf("SETDATE command requires year, month, and day\n");
    } else {
        int year = strtoul(arguments[0], NULL, 10);
        int month = strtoul(arguments[1], NULL, 10);
        int day = strtoul(arguments[2], NULL, 10);
        write_date(year, month, day);
    }
}

void cmd_irq(int arg_count, const char** arguments) {
    //int syscall_index = 0;  // Index of `kernel_hello`
    if (arg_count == 0) {
        printf("IRQ command without arguments\n");

    } else {
        //syscall_index = strtoul(arguments[0], NULL, 10);

        // send irq
        int irq = strtoul(arguments[0], NULL, 10);
        __asm__ volatile("int $0x2b\n" : : "a"(irq) : "memory");

    }

    //    //int syscall_index = 1;  // Syscall index
    //     int parameter = 1000;     // First argument
    //     int parameter1 = 20;    // Second argument
    //     int parameter2 = 30;    // Third argument
    //     int parameter3 = 40;    // Fourth argument

    //     __asm__ volatile(
    //         "int $0x80\n"       // Trigger syscall interrupt
    //         :
    //         : "a"(syscall_index), "b"(parameter), "c"(parameter1), "d"(parameter2), "e"(parameter3)
    //         : "memory"
    //     );

    //     printf("Return from syscall index: %d, Arguments: %d, %d, %d\n", syscall_index, parameter, parameter1, parameter2);
}

// TODO: Implement sleep function
void cmd_sleep(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("SLEEP command without arguments\n");
    } else {
        int seconds = strtoul(arguments[0], NULL, 10);
        printf("Sleeping for %d seconds\n", seconds);
        delay_ms(seconds * 1000);

        printf("Sleeping for %d seconds finished.\n", seconds);
    }
}
// TODO: Implement exit function
void cmd_exit(int arg_count, const char** arguments) {
    printf("Exiting command interpreter\n");
    // Implement necessary cleanup and exit logic for the kernel or environment
    exit(0);
}

void cmd_fdd(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        debug_read_bootsector();
    } else {

        // int sector = strtoul(arguments[0], NULL, 10);

        // printf("Reading sector %d\n", sector);

        // debug_read_bootsector(sector);
    }
}

void cmd_hdd(int arg_count, const char** arguments) {

    printf("HDD debug command\n");

    // check if current drive is set
    if (current_drive == NULL) {
        printf("No current drive set\n");
    } else {
        // print debug info about current drive
        printf("Current drive: %s\n", current_drive->name);
        ata_debug_bootsector(current_drive);
    }
}

void cmd_beep(int arg_count, const char** arguments) {
    if (arg_count < 2) {
        //printf("BEEP command requires frequency and duration\n");
        beep(1000, 1000);
    } else {
        uint32_t frequency = strtoul(arguments[0], NULL, 10);
        uint32_t duration = strtoul(arguments[1], NULL, 10);
        beep(frequency, duration);
    }
}

void cmd_wait(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("WAIT command without arguments\n");
    } else {
        int ticks = strtoul(arguments[0], NULL, 10);
        printf("Sleeping for %d ticks...\n", ticks);
        delay_ms(ticks);
        printf("Done sleeping!\n");
    }
}

void cmd_run(int arg_count, const char** arguments) {
    if (arg_count < 1) {
        printf("Usage: RUN <program.prg> [arguments...]\n");
        return;
    }
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(arguments[0], &resolved)) return;
    drive_t* saved_drive = current_drive;
    char working_directory[SHELL_PATH_MAX];
    if (shell_path_join_mount(saved_drive->mount_point, current_path,
                              working_directory) != SHELL_PATH_OK) {
        printf("Unable to determine the working directory.\n");
        shell_restore_drive(saved_drive);
        return;
    }
    int pid = create_process_for_file_args(resolved.vfs_path, arg_count,
                                           arguments, working_directory);
    shell_restore_drive(saved_drive);
    if (pid == -1) {
        printf("Failed to start program '%s'.\n", arguments[0]);
    } else {
        wait_for_process(pid);
    }
}

// Open the specified file and print its contents
void open_file(const char* path) {
    shell_resolved_path_t resolved;
    if (!shell_resolve_path(path, &resolved)) return;

    drive_t* saved_drive = current_drive;
    vfs_node_t* node = NULL;
    int result = vfs_open(resolved.vfs_path, &node);
    if (result != VFS_OK) {
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("TYPE", path, result);
        return;
    }
    if (node->type != VFS_FILE) {
        int type_error = node->type == VFS_DIRECTORY
            ? VFS_ERR_IS_DIR : VFS_ERR_UNSUPPORTED;
        (void)vfs_close(node);
        shell_restore_drive(saved_drive);
        shell_print_vfs_error("TYPE", path, type_error);
        return;
    }

    uint8_t buffer[512];
    uint32_t offset = 0;
    char last = '\n';
    bool wrote_anything = false;
    while (offset < node->size) {
        uint32_t amount = node->size - offset;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        result = vfs_read(node, offset, amount, buffer);
        if (result < 0) {
            (void)vfs_close(node);
            shell_restore_drive(saved_drive);
            shell_print_vfs_error("TYPE", path, result);
            return;
        }
        if (result == 0 || (uint32_t)result > amount) {
            (void)vfs_close(node);
            shell_restore_drive(saved_drive);
            shell_print_vfs_error("TYPE", path, VFS_ERR_IO);
            return;
        }
        for (int i = 0; i < result; ++i) putchar((char)buffer[i]);
        last = (char)buffer[result - 1];
        wrote_anything = true;
        offset += (uint32_t)result;
    }

    int close_result = vfs_close(node);
    shell_restore_drive(saved_drive);
    if (wrote_anything && last != '\n') putchar('\n');
    if (close_result != VFS_OK) {
        shell_print_vfs_error("TYPE", path, close_result);
    }
}

void cmd_list_processes(int arg_count, const char** arguments) {
    (void)arg_count;
    (void)arguments;
    list_running_processes();
}

void cmd_start_task(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("RTASK command without arguments\n");
        return;
    }

    int task_id = strtoul(arguments[0], NULL, 10);
    if (task_id < 0 || task_id >= MAX_TASKS) {
        printf("Invalid task ID: %d\n", task_id);
        return;
    }

    //start_task(task_id);
}

void cmd_net(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("NET command - Network interface management\n");
        printf("Usage:\n");
        printf("  NET STATUS  - Show network interface status\n");
        printf("  NET SEND    - Send test packet\n");
        printf("  NET INFO    - Show detailed network information\n");
        printf("  NET DEBUG   - Show E1000 register dump\n");
        printf("  NET DHCP    - Request or renew the LAN address via DHCP\n");
        printf("  NET LISTEN [n] - Listen for incoming packets (n=count, default 10)\n");
        printf("  NET RECV    - Try to receive one packet\n");
        return;
    }

    if (strcmp(arguments[0], "STATUS") == 0 || strcmp(arguments[0], "status") == 0) {
        // Show network status for all network cards
        bool has_network = false;
        
        if (rtl8139_is_initialized()) {
            printf("Network card: Realtek RTL8139 (initialized)\n");
            uint8_t mac[6];
            rtl8139_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            has_network = true;
        }
        
        if (e1000_is_initialized()) {
            printf("Network card: Intel E1000 (initialized)\n");
            uint8_t mac[6];
            e1000_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            has_network = true;
        }
        
        if (ne2000_is_initialized()) {
            printf("Network card: NE2000 compatible (initialized)\n");
            uint8_t mac[6];
            ne2000_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            has_network = true;
        }
        
        if (!has_network) {
            printf("No network card initialized\n");
        }
    } else if (strcmp(arguments[0], "INFO") == 0 || strcmp(arguments[0], "info") == 0) {
        // Get detailed network interface information
        bool has_info = false;
        
        if (rtl8139_is_initialized()) {
            printf("RTL8139 Network Adapter Info:\n");
            uint8_t mac[6];
            rtl8139_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            printf("  Status: Initialized and ready\n");
            printf("  Driver: Realtek RTL8139 (PCI 10EC:8139)\n");
            has_info = true;
        }
        
        if (e1000_is_initialized()) {
            if (has_info) printf("\n");  // Separator if both adapters present
            printf("E1000 Network Adapter Info:\n");
            uint8_t mac[6];
            e1000_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            printf("  Status: Initialized and ready\n");
            printf("  Driver: Intel E1000 (PCI 8086:100E)\n");
            has_info = true;
        }
        
        if (ne2000_is_initialized()) {
            if (has_info) printf("\n");  // Separator if both adapters present
            printf("NE2000 Network Adapter Info:\n");
            uint8_t mac[6];
            ne2000_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            printf("  Status: Initialized and ready\n");
            printf("  Driver: NE2000 compatible (PCI 10EC:8029)\n");
            has_info = true;
        }
        
        if (!has_info) {
            printf("No network card initialized\n");
        }
    } else if (strcmp(arguments[0], "DEBUG") == 0 || strcmp(arguments[0], "debug") == 0) {
        // Show network debug info
        if (rtl8139_is_initialized()) {
            printf("RTL8139 Debug Info:\n");
            uint8_t mac[6];
            rtl8139_get_mac_address(mac);
            printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            printf("  (Register dump not yet implemented for RTL8139)\n");
        } else if (e1000_is_initialized()) {
            e1000_debug_registers();
            netstack_debug_stats();
            
            // Try to manually check for packets
            printf("\nManually checking for packets...\n");
            uint8_t buffer[2048];
            netdev_poll();
            int len = netdev_receive_frame(buffer, sizeof(buffer));
            if (len > 0) {
                printf("Found packet! Length: %d bytes\n", len);
                // Print first 64 bytes
                printf("Data: ");
                for (int i = 0; i < (len < 64 ? len : 64); i++) {
                    printf("%02X ", buffer[i]);
                    if ((i + 1) % 16 == 0) printf("\n      ");
                }
                printf("\n");
            } else {
                printf("No packets in RX ring\n");
            }
        } else {
            printf("E1000 not initialized\n");
        }
    } else if (strcmp(arguments[0], "DHCP") == 0 ||
               strcmp(arguments[0], "dhcp") == 0) {
        if (!netdev_available()) {
            printf("Network card not initialized.\n");
            return;
        }
        if (!netstack_configure_dhcp()) {
            printf("No DHCP lease received. Check the VMnet0 bridge and LAN.\n");
        }
    } else if(strcmp(arguments[0], "SEND") == 0 || strcmp(arguments[0], "send") == 0) {
        // Send a test packet
        if (rtl8139_is_initialized()) {
            printf("Sending test packet via RTL8139...\n");
            rtl8139_send_test_packet();
            printf("Test packet sent.\n");
        } else if (e1000_is_initialized()) {
            printf("Sending test packet via E1000...\n");
            e1000_send_test_packet();
            printf("Test packet sent.\n");
        } else if (ne2000_is_initialized()) {
            printf("Sending test packet via NE2000...\n");
            ne2000_test_send();
            printf("Test packet sent.\n");
        } else {
            printf("Network card not initialized. Cannot send packet.\n");
            return;
        }
    } else if(strcmp(arguments[0], "LISTEN") == 0 || strcmp(arguments[0], "listen") == 0) {
        // Listen for incoming packets
        if (!netdev_available()) {
            printf("Network card not initialized.\n");
            return;
        }
        printf("Using %s adapter\n", netdev_backend_name());
        
        int max_packets = 10;  // Default
        if (arg_count > 1) {
            max_packets = atoi(arguments[1]);
            if (max_packets <= 0 || max_packets > 100) {
                printf("Invalid packet count. Using default (10).\n");
                max_packets = 10;
            }
        }
        
        printf("Listening for up to %d packets... (Press Ctrl+C to stop)\n", max_packets);
        printf("Waiting for network traffic...\n");
        
        uint8_t buffer[1518];
        int packets_received = 0;
        netdev_reset_monitor();
        
        for (int i = 0; i < max_packets * 100000; i++) {
            netdev_poll();
            int len = netdev_receive_frame(buffer, sizeof(buffer));
            if (len > 0) {
                packets_received++;
                printf("\n[Packet %d] Received %d bytes:\n", packets_received, len);
                
                // Print packet header info
                if (len >= 14) {
                    printf("  Dst MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                           buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
                    printf("  Src MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                           buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11]);
                    printf("  EtherType: 0x%04X\n", (buffer[12] << 8) | buffer[13]);
                }
                
                // Print first 64 bytes of payload
                printf("  Data: ");
                int print_len = len < 64 ? len : 64;
                for (int j = 0; j < print_len; j++) {
                    printf("%02X ", buffer[j]);
                    if ((j + 1) % 16 == 0) printf("\n        ");
                }
                printf("\n");
                
                if (packets_received >= max_packets) {
                    break;
                }
            }
            
            // Small delay between checks
            if (i % 10000 == 0) {
                printf(".");  // Progress indicator
            }
        }
        
        printf("\n%d packet(s) received.\n", packets_received);
        
    } else if(strcmp(arguments[0], "RECV") == 0 || strcmp(arguments[0], "recv") == 0) {
        // Try to receive one packet
        uint8_t buffer[1518];
        
        if (!netdev_available()) {
            printf("Network card not initialized.\n");
            return;
        }
        netdev_poll();
        int len = netdev_receive_frame(buffer, sizeof(buffer));
        
        if (len > 0) {
            printf("Received %d bytes:\n", len);
            for (int i = 0; i < len && i < 128; i++) {
                printf("%02X ", buffer[i]);
                if ((i + 1) % 16 == 0) printf("\n");
            }
            printf("\n");
        } else {
            printf("No packet available.\n");
        }
    } else {
        printf("Unknown NET command: %s\n", arguments[0]);
        printf("Type 'NET' without arguments for help\n");
    }
}

void cmd_ifconfig(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("IFCONFIG - Configure network interface\n");
        printf("Usage: ifconfig dhcp\n");
        printf("Usage: ifconfig <ip> <netmask> <gateway>\n");
        printf("Example: ifconfig 192.168.1.50 255.255.255.0 192.168.1.1\n");
        return;
    }

    if (arg_count == 1 &&
        (strcmp(arguments[0], "dhcp") == 0 ||
         strcmp(arguments[0], "DHCP") == 0)) {
        if (!netstack_configure_dhcp()) {
            printf("No DHCP lease received. Check the VMnet0 bridge and LAN.\n");
        }
        return;
    }
    
    if (arg_count < 3) {
        printf("Error: Requires 3 arguments (IP, netmask, gateway)\n");
        return;
    }
    
    uint32_t ip = parse_ipv4(arguments[0]);
    uint32_t netmask = parse_ipv4(arguments[1]);
    uint32_t gateway = parse_ipv4(arguments[2]);
    
    if (ip == 0 || netmask == 0 || gateway == 0) {
        printf("Error: Invalid IP address format\n");
        return;
    }
    
    netstack_set_config(ip, netmask, gateway);
    printf("Network interface configured successfully\n");
}

void cmd_ping(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("PING - Send ICMP echo requests until Ctrl+C\n");
        printf("Usage: ping <ip_address>\n");
        printf("Example: ping 192.168.1.1\n");
        return;
    }

    uint32_t target_ip = parse_ipv4(arguments[0]);
    if (target_ip == 0) {
        printf("Error: Invalid IP address\n");
        return;
    }

    static uint16_t ping_id = 0x1234;
    static uint16_t seq = 1;

    if (netstack_get_ip_address() == 0) {
        printf("PING aborted: no local IP address\n");
        return;
    }

    uint32_t sent = 0;
    uint32_t received = 0;
    bool interrupted = false;

    printf("PING %s continuously (Ctrl+C to stop)\n", arguments[0]);
    while (!interrupted) {
        uint16_t current_seq = seq++;
        ++sent;
        printf("PING %s (id=0x%04X, seq=%u)... ",
               arguments[0], ping_id, current_seq);
        if (netstack_ping(target_ip, ping_id, current_seq, 2000u)) {
            ++received;
            printf("reply received\n");
        } else {
            printf("timeout or unreachable\n");
        }

        /* Keep the usual one-second ping cadence while remaining responsive
         * to both PS/2 keyboard and serial-console Ctrl+C input. */
        for (uint32_t elapsed = 0; elapsed < 1000u; elapsed += 10u) {
            char ch = getchar_nonblocking();
            if (ch == 0x03) {
                interrupted = true;
                break;
            }
            netdev_poll();
            pit_delay(10u);
        }
    }

    printf("^C\n--- %s ping statistics ---\n", arguments[0]);
    printf("%u packets transmitted, %u received, %u lost\n",
           sent, received, sent - received);
}

void cmd_arp(int arg_count, const char** arguments) {
    printf("ARP - Address Resolution Protocol\n");
    printf("Commands:\n");
    printf("  arp scan <ip> - Send ARP request to IP\n");
    printf("  arp cache     - Show ARP cache (not yet implemented)\n");
    
    if (arg_count > 0 && strcmp(arguments[0], "scan") == 0) {
        if (arg_count < 2) {
            printf("Usage: arp scan <ip_address>\n");
            return;
        }
        
        uint32_t target_ip = parse_ipv4(arguments[1]);
        if (target_ip == 0) {
            printf("Error: Invalid IP address\n");
            return;
        }
        
        printf("Sending ARP request to %s...\n", arguments[1]);
        arp_send_request(target_ip);
    }
}

/**
 * Display command history
 */
void cmd_history(int arg_count, const char** arguments) {
    history_list();
}

/**
 * Launch BASIC interpreter
 */
void cmd_pci(int arg_count, const char **args) {
    if (pci_device_count == 0) {
        printf("No PCI devices detected\n");
        return;
    }

    printf("\nDetected PCI devices: %u\n", (unsigned)pci_device_count);
    for (size_t i = 0; i < pci_device_count; i++) {
        pci_device_t *d = &pci_devices[i];
        printf("[%02u] Bus %u Slot %u Func %u  Vendor:0x%04X Device:0x%04X\n",
               (unsigned)i, d->bus, d->slot, d->function,
               (unsigned)d->vendor_id, (unsigned)d->device_id);
        printf("     Class: 0x%02X Subclass: 0x%02X ProgIF: 0x%02X Rev: 0x%02X Header: 0x%02X IRQ: %u\n",
               (unsigned)d->class_code, (unsigned)d->subclass_code,
               (unsigned)d->prog_if, (unsigned)d->revision_id,
               (unsigned)d->header_type, (unsigned)d->irq_line);

        for (int b = 0; b < 6; b++) {
            if (d->bar[b] != 0) {
                printf("     BAR%-1d: 0x%08X\n", b, (unsigned)d->bar[b]);
            }
        }
        printf("\n");
    }
}

// Print the current IP address
void cmd_get_ip(int argc, const char **argv) {
    (void)argc; (void)argv;

    uint32_t ip = netstack_get_ip_address();
    char ip_str[16];
    format_ipv4(ip, ip_str);

    for (const char *p = ip_str; *p; ++p) putchar(*p);
    putchar('\n');
}
