#include "kernel/shell/command.h"
#include "kernel/proc/process.h"
#include "kernel/init/prg.h"
#include "arch/x86/include/sys.h"
#include "kernel/sched/scheduler.h"
#include "mm/kmalloc.h"

#include "drivers/char/rtc.h"
#include "drivers/block/ata.h"
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
// #include "drivers/net/vmxnet3.h"

char current_path[256] = "/";
extern pci_device_t pci_devices[];
extern size_t pci_device_count;

// Forward declarations
int split_input(const char* input, char* command, char** arguments, int max_length, int max_args);
void open_file(const char* path);
void free_arguments(char** arguments, int arg_count);

bool is_null_terminated(char* buffer, size_t max_length) {
    for (size_t i = 0; i < max_length; i++) {
        if (buffer[i] == '\0') {
            return true; // Found the null terminator
        }
    }
    return false; // No null terminator found within max_length
}

#define MAX_COMMANDS 100
#define MAX_LINE_LENGTH 128

typedef void (*command_func)(int cnt, const char **args);

// Command structure
typedef struct {
    char *name;
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
void list_running_processes(int cnt, const char **args);
void cmd_start_task(int cnt, const char **args);
void cmd_net(int cnt, const char **args);
void cmd_ifconfig(int cnt, const char **args);
void cmd_ping(int cnt, const char **args);
void cmd_arp(int cnt, const char **args);
void cmd_history(int cnt, const char **args);
void cmd_basic(int cnt, const char **args);

// Command table
command_t command_table[MAX_COMMANDS] = {
    {"help", cmd_help},
    {"clear", cmd_clear},
    {"echo", cmd_echo},
    {"mem", cmd_mem},
    {"dump", cmd_dump},
    {"cls", cmd_cls},
    {"ls", cmd_ls},
    {"cd", cmd_cd},
    {"drives", cmd_drives},
    {"mount", cmd_mount},
    {"mkdir", cmd_mkdir},
    {"rmdir", cmd_rmdir},
    {"mkfile", cmd_mkfile},
    {"rmfile", cmd_rmfile},
    {"run", cmd_run},
    {"exec", cmd_exec},
    {"kill", cmd_kill},
    {"sys", cmd_sys},
    {"open", cmd_open},
    {"datetime", cmd_read_datetime},
    {"settime", cmd_set_time},
    {"setdate", cmd_set_date},
    {"irq", cmd_irq},
    {"sleep", cmd_sleep},
    {"exit", cmd_exit},
    {"fdd", cmd_fdd},
    {"hdd", cmd_hdd},
    {"beep", cmd_beep},
    {"wait", cmd_wait},
    {"pid", (command_func)list_running_processes},
    {"rtask", cmd_start_task},
    {"net", cmd_net},
    {"ifconfig", cmd_ifconfig},
    {"ping", cmd_ping},
    {"arp", cmd_arp},
    {"history", cmd_history},
    {"basic", cmd_basic},
    {"pci", cmd_pci},
    {NULL, NULL} // End marker
};

#define MAX_ARGS 10
#define MAX_LENGTH 64

/**
 * Parse path for drive prefix (e.g., /hdd0/dir or hdd0:/dir)
 * Returns drive name if found, NULL otherwise
 * Also modifies path_out to point to remainder after drive prefix
 */
const char* extract_drive_from_path(const char* path, char** path_out) {
    static char drive_name[16];
    
    if (!path || !path_out) {
        return NULL;
    }
    
    *path_out = (char*)path;  // Default: no modification
    
    // Check for /drivename/path format (e.g., /hdd0/dir/file)
    if (path[0] == '/') {
        const char* next_slash = strchr(path + 1, '/');
        size_t drive_len = next_slash ? (next_slash - path - 1) : strlen(path + 1);
        
        // Check if it's a valid drive name length and pattern
        if (drive_len == 4 && (strncmp(path + 1, "hdd", 3) == 0 || strncmp(path + 1, "fdd", 3) == 0)) {
            strncpy(drive_name, path + 1, 4);
            drive_name[4] = '\0';
            str_to_lower(drive_name);
            
            // Point to remainder of path (or "/" if nothing after drive)
            *path_out = next_slash ? (char*)next_slash : (char*)"/";
            return drive_name;
        }
    }
    
    // Check for drivename:/path format (e.g., hdd0:/dir/file)
    if (strlen(path) >= 5 && path[4] == ':' && 
        (strncmp(path, "hdd", 3) == 0 || strncmp(path, "fdd", 3) == 0)) {
        strncpy(drive_name, path, 4);
        drive_name[4] = '\0';
        str_to_lower(drive_name);
        
        // Point to remainder of path (skip the colon)
        *path_out = (char*)(path + 5);
        if (*path_out[0] == '\0') {
            *path_out = (char*)"/";  // Empty path after colon = root
        }
        return drive_name;
    }
    
    return NULL;  // No drive prefix found
}

/**
 * Try to switch to a drive by name (hdd0, hdd1, fdd0, etc.)
 * Returns true if successful, false if not a drive name
 */
bool try_switch_drive(const char* name) {
    extern drive_t* current_drive;
    
    // Convert to lowercase for comparison
    char drive_name[16];
    strncpy(drive_name, name, sizeof(drive_name) - 1);
    drive_name[sizeof(drive_name) - 1] = '\0';
    str_to_lower(drive_name);
    
    // Check if it matches drive name pattern (hdd0-9, fdd0-9)
    if ((strncmp(drive_name, "hdd", 3) == 0 || strncmp(drive_name, "fdd", 3) == 0) &&
        strlen(drive_name) == 4 && drive_name[3] >= '0' && drive_name[3] <= '9') {
        
        // Try to find and switch to this drive
        drive_t* drive = get_drive_by_name(drive_name);
        if (drive) {
            current_drive = drive;
            printf("Switched to drive %s\n", drive->name);
            
            // Reset path to root when switching drives
            strncpy(current_path, "/", sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
            
            return true;
        } else {
            printf("Drive %s not found or not mounted\n", drive_name);
            return true;  // Still consumed the input, just wasn't successful
        }
    }
    
    return false;  // Not a drive name
}

void process_command(char *input_buffer) {
    char command[MAX_LENGTH];
    char* arguments[MAX_ARGS] = {NULL};  // Initialize all pointers to NULL
    int arg_cnt = split_input(input_buffer, command, arguments, MAX_LENGTH, MAX_ARGS);

    // Check for memory allocation failure
    if (arg_cnt < 0) {
        printf("Error: Failed to parse command arguments\n");
        return;
    }

    // Check if the command is empty
    if (command[0] == '\0') {
        free_arguments(arguments, arg_cnt);  // Free allocated arguments
        return; // Empty input
    }
    
    // Check if input is a drive name (hdd0, hdd1, fdd0, etc.)
    if (try_switch_drive(command)) {
        free_arguments(arguments, arg_cnt);
        return;
    }

    // Convert only the command to uppercase (not the arguments!)
    str_to_upper(command);

    // Match command
    int found = 0;
    for (int i = 0; command_table[i].name != NULL; i++) {

        str_to_upper(command_table[i].name);

        if (strcmp(command, command_table[i].name) == 0) {
            command_table[i].execute(arg_cnt, (const char**)arguments);
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("\nUnknown command: %s\n", command);
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
        // At newest, return to empty line
        history_current = -1;
        return "";
    }
    
    history_current = (history_current + 1) % HISTORY_SIZE;
    return history_buffer[history_current];
}

/**
 * Reset history browsing to newest
 */
void history_reset(void) {
    history_current = -1;
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
    if (len >= 127) len = 127;
    
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
 * Handle arrow keys and special sequences
 */
static bool handle_escape_sequence(char *buffer, int *buffer_index, int *cursor_pos) {
    // Read next character
    char seq1 = input_queue_pop();
    if (seq1 == '\0') return false;
    
    if (seq1 != '[') {
        // Not a recognized sequence
        return false;
    }
    
    // Read key code
    char key_code = input_queue_pop();
    if (key_code == '\0') return false;
    
    switch (key_code) {
        case KEY_UP: {
            // Previous command in history
            const char *prev = history_get_prev();
            if (prev != NULL) {
                replace_current_line(buffer, prev, cursor_pos, buffer_index);
            }
            return true;
        }
        
        case KEY_DOWN: {
            // Next command in history
            const char *next = history_get_next();
            if (next != NULL) {
                replace_current_line(buffer, next, cursor_pos, buffer_index);
            }
            return true;
        }
        
        case KEY_LEFT:
            // Move cursor left within line
            if (*cursor_pos > 0) {
                (*cursor_pos)--;
                vga_move_cursor_left();
            }
            return true;
            
        case KEY_RIGHT:
            // Move cursor right within line
            if (*cursor_pos < *buffer_index) {
                (*cursor_pos)++;
                vga_move_cursor_right();
            }
            return true;
            
        case KEY_HOME:
            // Jump to start of line (after prompt)
            while (*cursor_pos > 0) {
                (*cursor_pos)--;
                vga_move_cursor_left();
            }
            return true;
            
        case KEY_END:
            // Jump to end of line
            while (*cursor_pos < *buffer_index) {
                (*cursor_pos)++;
                vga_move_cursor_right();
            }
            return true;
            
        case KEY_DELETE:
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
        printf("%s> ", current_drive->name);
    } else {
        show_prompt();
    }
}

void command_loop() {
    printf("+++Enhanced shell with line editing and history started\n");
    show_prompt();

    char* input = (char*)k_malloc(256);
    if (input == NULL) {
        printf("Failed to allocate memory for input buffer\n");
        return;
    }

    int buffer_index = 0;  // End of text in buffer
    int cursor_pos = 0;     // Current cursor position (0 to buffer_index)
    input[0] = '\0';

    while (1) {
        // Get keyboard or serial input (non-blocking, checks both sources)
        char ch = getchar_nonblocking();
        
        if (ch != 0) {
            // Check for escape sequences (arrow keys, etc.)
            if (ch == '\0') {  // ESC
                if (handle_escape_sequence(input, &buffer_index, &cursor_pos)) {
                    continue;
                }
            }
            // Check for Ctrl+key combinations
            else if (ch < 0x20 && ch != '\n' && ch != '\t' && ch != '\b') {
                if (handle_ctrl_key(ch, input, &buffer_index, &cursor_pos)) {
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
                if (buffer_index < 255) {
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
int split_input(const char* input, char* command, char** arguments, int max_length, int max_args) {
    int i = 0, j = 0, arg_count = 0;

    // Skip leading spaces
    while (input[i] == ' ' && input[i] != '\0') {
        i++;
    }

    // Extract the command
    while (input[i] != ' ' && input[i] != '\0' && j < max_length - 1) {
        command[j++] = input[i++];
    }
    command[j] = '\0';  // Null-terminate the command

    // Skip spaces after the command
    while (input[i] == ' ' && input[i] != '\0') {
        i++;
    }

    // Extract arguments
    while (input[i] != '\0' && arg_count < max_args) {
        j = 0;

        // Allocate memory for the argument
        arguments[arg_count] = (char*)malloc(max_length);
        if (arguments[arg_count] == NULL) {
            // Malloc failed - free previously allocated arguments
            printf("Error: Memory allocation failed for argument %d\n", arg_count);
            for (int k = 0; k < arg_count; k++) {
                free(arguments[k]);
                arguments[k] = NULL;
            }
            return -1;  // Return -1 to indicate error
        }

        // Parse the argument
        while (input[i] != ' ' && input[i] != '\0' && j < max_length - 1) {
            arguments[arg_count][j++] = input[i++];
        }
        arguments[arg_count][j] = '\0';  // Null-terminate the argument
        arg_count++;

        // Skip spaces between arguments
        while (input[i] == ' ' && input[i] != '\0') {
            i++;
        }
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
    printf("\n=== Rudolf Stepan x86 Microkernel Shell ===\n\n");
    printf("Enhanced shell with line editing and command history\n");
    printf("\nKeyboard Shortcuts:\n");
    printf("  Up Arrow    - Previous command in history\n");
    printf("  Down Arrow  - Next command in history\n");
    printf("  Ctrl+C      - Cancel current line\n");
    printf("  Ctrl+L      - Clear screen\n");
    printf("  Ctrl+U      - Clear entire line\n");
    printf("  Ctrl+K      - Clear from cursor to end of line\n");
    printf("  Backspace   - Delete character before cursor\n");
    printf("  Delete      - Delete character at cursor\n");
    
    printf("\nAvailable Commands:\n");
    /* Print commands in multiple columns (1-3) depending on how many commands exist */
    int cmd_count = 0;
    while (command_table[cmd_count].name != NULL) cmd_count++;
    int cols = 3;
    if (cmd_count < 6) cols = 1;
    else if (cmd_count < 20) cols = 2;
    int rows = (cmd_count + cols - 1) / cols;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = c * rows + r;
            if (idx < cmd_count) {
                /* Two leading spaces then a left-aligned column of width 20 */
                printf("  %-20s", command_table[idx].name);
            }
        }
        printf("\n");
    }
    
    printf("\nTip: Use 'history' to see previous commands\n");
    printf("     Use arrow keys to navigate through history\n\n");
}

void cmd_clear(int arg_count, const char **args) {
    clear_screen();
}

void cmd_echo(int arg_count, const char **args) {
    if(arg_count == 0) {
        printf("Echo command without arguments\n");
    } else {
        for (int i = 0; i < arg_count; i++) {
            printf("%s ", args[i]);
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
    printf("Available drives:\n");
    list_detected_drives();
}

/// @brief Mount an attached drive
/// @param arg_count 
/// @param arguments 
void cmd_mount(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("Mount command without arguments\n");
        printf("Available drives:\n");
        list_detected_drives();
    } else {
        str_to_lower((char *)arguments[0]);
        printf("Try mount drive: %s\n", arguments[0]);
        
        // Debug: show what drives are available
        printf("Searching in %d detected drives...\n", drive_count);
        
        current_drive = get_drive_by_name(arguments[0]);
        if (current_drive == NULL) {
            printf("drive: %s not found\n", arguments[0]);
            printf("Available drives:\n");
            list_detected_drives();
        } else {
            // Check if already mounted
            if (current_drive->mount_point[0] != '\0') {
                printf("Drive %s already mounted at %s\n", 
                       current_drive->name, current_drive->mount_point);
                strncpy(current_path, "/", sizeof(current_path) - 1);
                current_path[sizeof(current_path) - 1] = '\0';
                return;
            }
            
            printf("Mounting drive %s...\n", current_drive->name);

            const char* fs_type = "fat32";  // Default
            char mount_path[64];
            int result;
            
            switch (current_drive->type) {
            case DRIVE_TYPE_ATA: {
                // Detect filesystem type
                uint8_t buffer[512];
                if (!ata_read_sector(current_drive->base, 0, buffer, current_drive->is_master)) {
                    printf("Failed to read boot sector from %s\n", current_drive->name);
                    return;
                }
                
                // Check for EXT2 (magic at byte 1080 = sector 2, offset 56)
                uint8_t ext2_buffer[1024];
                if (ata_read_sector(current_drive->base, 2, ext2_buffer, current_drive->is_master)) {
                    if (ata_read_sector(current_drive->base, 3, ext2_buffer + 512, current_drive->is_master)) {
                        uint16_t magic = *(uint16_t*)(ext2_buffer + 56);
                        if (magic == 0xEF53) {
                            fs_type = "ext2";
                            printf("Detected EXT2 filesystem\n");
                        } else {
                            printf("Detected FAT32 filesystem\n");
                        }
                    }
                }
                
                // Create mount path
                snprintf(mount_path, sizeof(mount_path), "/mnt/%s", current_drive->name);
                
                // Mount using VFS
                result = vfs_mount(current_drive, fs_type, mount_path);
                if (result == VFS_OK) {
                    strncpy(current_drive->mount_point, mount_path, sizeof(current_drive->mount_point) - 1);
                    current_drive->mount_point[sizeof(current_drive->mount_point) - 1] = '\0';
                    printf("Successfully mounted %s at %s (%s)\n", 
                           current_drive->name, mount_path, fs_type);
                } else {
                    printf("Failed to mount %s (VFS error %d)\n", current_drive->name, result);
                    return;
                }
                break;
            }
            case DRIVE_TYPE_FDD:
                printf("Mounting floppy drive %s\n", current_drive->name);
                snprintf(mount_path, sizeof(mount_path), "/mnt/%s", current_drive->name);
                
                result = vfs_mount(current_drive, "fat12", mount_path);
                if (result == VFS_OK) {
                    strncpy(current_drive->mount_point, mount_path, sizeof(current_drive->mount_point) - 1);
                    current_drive->mount_point[sizeof(current_drive->mount_point) - 1] = '\0';
                    printf("Successfully mounted %s at %s (FAT12)\n", 
                           current_drive->name, mount_path);
                } else {
                    printf("Failed to mount %s (VFS error %d)\n", current_drive->name, result);
                    return;
                }
                break;

            default:
                printf("Unsupported drive type\n");
                return;
            }

            // Safe string copy with bounds checking
            strncpy(current_path, "/", sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
        }
    }
}

/// @brief List the directory content
/// @param arg_count 
/// @param arguments 
void cmd_ls(int arg_count, const char** arguments) {
    extern drive_t* current_drive;
    const char* directory = (arg_count == 0) ? current_path : arguments[0];
    
    // Check for drive prefix in path
    if (arg_count > 0) {
        char* path_remainder = NULL;
        const char* drive_name = extract_drive_from_path(arguments[0], &path_remainder);
        
        if (drive_name) {
            // Switch to specified drive temporarily
            drive_t* original_drive = current_drive;
            if (try_switch_drive(drive_name)) {
                directory = path_remainder;
            } else {
                return;  // Drive switch failed, error already printed
            }
        }
    }
    
    if (current_drive == NULL) {
        printf("No drive mounted\n");
        return;
    }
    
    // Build full VFS path based on current drive's mount point
    char vfs_path[256];
    if (current_drive->mount_point && strlen(current_drive->mount_point) > 0) {
        // Drive has explicit mount point
        if (strcmp(current_drive->mount_point, "/") == 0) {
            // Mounted at root
            strncpy(vfs_path, directory, sizeof(vfs_path) - 1);
        } else {
            // Mounted at /mnt/<name> or similar
            if (strcmp(directory, "/") == 0) {
                strncpy(vfs_path, current_drive->mount_point, sizeof(vfs_path) - 1);
            } else {
                snprintf(vfs_path, sizeof(vfs_path), "%s%s", 
                         current_drive->mount_point, directory);
            }
        }
        vfs_path[sizeof(vfs_path) - 1] = '\0';
    } else {
        // Fallback: assume mounted at /mnt/<drive_name>
        if (strcmp(directory, "/") == 0) {
            snprintf(vfs_path, sizeof(vfs_path), "/mnt/%s", current_drive->name);
        } else {
            snprintf(vfs_path, sizeof(vfs_path), "/mnt/%s%s", 
                     current_drive->name, directory);
        }
    }
    
    // Use VFS for all filesystems
    printf("\nDirectory of %s (vfs: %s)\n", directory, vfs_path);
    printf("[DEBUG] current_drive=%s, mount_point='%s'\n", 
           current_drive->name, current_drive->mount_point);
    printf("%-40s %-10s %-8s\n", "FILENAME", "SIZE", "TYPE");
    printf("--------------------------------------------------------------------------------\n");
    
    uint32_t index = 0;
    vfs_dir_entry_t entry;
    int result;
    
    printf("[DEBUG] Calling vfs_readdir with path='%s', index=%u\n", vfs_path, index);
    while ((result = vfs_readdir(vfs_path, index, &entry)) == VFS_OK) {
        // Format type
        const char* type_str = "";
        switch (entry.type) {
            case VFS_FILE: type_str = "FILE"; break;
            case VFS_DIRECTORY: type_str = "<DIR>"; break;
            case VFS_SYMLINK: type_str = "<LNK>"; break;
            default: type_str = "????"; break;
        }
        
        printf("%-40s %10u %-8s\n",
               entry.name, entry.size, type_str);
        
        index++;
    }
    
    if (index == 0) {
        printf("(empty directory)\n");
    }
    
    printf("\n");
}

void cmd_cd(int arg_count, const char** arguments) {
    extern drive_t* current_drive;
    
    if (arg_count == 0) {
        printf("CD command without arguments\n");
    } else {
        const char* target_path = arguments[0];
        
        // Check for drive prefix in path
        char* path_remainder = NULL;
        const char* drive_name = extract_drive_from_path(arguments[0], &path_remainder);
        
        if (drive_name) {
            // Permanently switch to specified drive
            if (!try_switch_drive(drive_name)) {
                return;  // Drive switch failed
            }
            target_path = path_remainder;
        }
        
        if (current_drive == NULL) {
            printf("No drive mounted\n");
            return;
        }
        
        str_trim_end((char*)target_path, '/');  // Remove trailing slash from the path
        char new_path[256] = "/";
        snprintf(new_path, sizeof(new_path), "%s/%s", current_path, target_path);

        if (current_drive->type == DRIVE_TYPE_ATA) {
            if (fat32_change_directory(new_path)) {
                // Safe string copy with bounds checking
                strncpy(current_path, new_path, sizeof(current_path) - 1);
                current_path[sizeof(current_path) - 1] = '\0';
                printf("Set directory to: %s\n", arguments[0]);
            }
        } else if (current_drive->type == DRIVE_TYPE_FDD) {
            // notice that the path is relative to the current directory
            // remove the leading slash
            if (new_path[0] == '/') {
                memmove(new_path, new_path + 1, strlen(new_path));
            }
            // fdd need relative path and a single directory name
            if (fat12_change_directory(arguments[0])) {
                // Safe string copy with bounds checking
                strncpy(current_path, new_path, sizeof(current_path) - 1);
                current_path[sizeof(current_path) - 1] = '\0';
                printf("Set directory to: %s\n", arguments[0]);
            }
        }
    }
}

void cmd_mkdir(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("MKDIR command without arguments\n");
    } else {
        mkdir(arguments[0], 0);
    }
}

void cmd_rmdir(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("RMDIR command without arguments\n");
    } else {
        rmdir(arguments[0]);
    }
}

void cmd_mkfile(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("MKFILE command without arguments\n");
    } else {
        mkfile(arguments[0]);
    }
}

void cmd_rmfile(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("RMFILE command without arguments\n");
    } else {
        remove(arguments[0]);
    }
}

void cmd_exec(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("EXEC command without arguments\n");
    } else {
        create_process_for_file(arguments[0]);
    }
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
    if (arg_count == 0) {
        printf("OPEN command without arguments\n");
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
        debug_read_bootsector(1);
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
    if (arg_count == 0) {
        printf("RUN command without arguments\n");
        return;
    }
    char* program_name = (char*)arguments[0];
    
    int pid = create_process(program_name);
    if (pid == -1) {
        printf("Failed to start program '%s'.\n", program_name);
    }
}

// Open the specified file and print its contents
void open_file(const char* path) {
    printf("Opening file: %s\n", path);

    switch ((drive_type_t)current_drive->type) {
    case DRIVE_TYPE_ATA:
{
        FILE* file = fat32_open_file(path, "r");
        if (file == NULL) {
            printf("File not found: %s\n", path);
            return;
        }

        printf("Name: %s\n", file->name);
        printf("Size: %zu\n", file->size);

        // Calculate the size of the buffer based on the size of the file
        size_t buffer_size = file->size; // Use the file size as the buffer size directly

        // Allocate the buffer
        char* buffer = (char*)malloc(buffer_size +1);
        if (buffer == NULL) {
            printf("Failed to allocate memory for file buffer\n");
            return;
        }

        // Read the file into the buffer, passing the correct size
        int result = fat32_read_file(file, buffer, buffer_size, buffer_size); // Pass buffer_size as the buffer size
        if (result == 0) {
            printf("Failed to read file\n");
            free(buffer);  // Free buffer before returning
            if (file->ptr) free(file->ptr);  // Free file data buffer
            free(file);  // Free file structure
            return;
        }

        printf("Result: %d\n", result);

        printf("File contents:\n");
        printf("%s\n", buffer);

        // Free all allocated memory
        secure_free(buffer, buffer_size);  // Clear the buffer with correct size
        if (file->ptr) {
            free(file->ptr);  // Free the file's internal buffer
        }
        free(file);  // Free the FILE structure
        break;
}
    case DRIVE_TYPE_FDD:
    {
        // TODO: use the gerneric FILE structure to read the file
        fat12_file* file = fat12_open_file(path, "r");
        if (file == NULL) {
            printf("File not found: %s\n", path);
            return;
        }

        // Calculate the size of the buffer based on the size of the file
        size_t buffer_size = file->size; // Use the file size as the buffer size directly

        // Allocate the buffer
        char* buffer = (char*)malloc(sizeof(char) * buffer_size);
        if (buffer == NULL) {
            printf("Failed to allocate memory for file buffer\n");
            return;
        }

        // Read the file into the buffer, passing the correct size
        int result = fat12_read_file(file, buffer, buffer_size, file->size); // Pass buffer_size as the buffer size
        if (result == 0) {
            printf("Failed to read file\n");
            return;
        }

        printf("File contents:\n%s\n", buffer);
        hex_dump((unsigned char*)buffer, file->size);

        secure_free(buffer, sizeof(buffer));  // Clear the buffer
    }
    break;

    default:
        break;
    }
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
            
            // Try to manually check for packets
            printf("\nManually checking for packets...\n");
            uint8_t buffer[2048];
            int len = e1000_receive_packet(buffer, sizeof(buffer));
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
        bool has_adapter = false;
        if (e1000_is_initialized()) {
            has_adapter = true;
            printf("Using E1000 adapter\n");
        } else if (ne2000_is_initialized()) {
            has_adapter = true;
            printf("Using NE2000 adapter\n");
        }
        
        if (!has_adapter) {
            printf("Network card not initialized.\n");
            return;
        }
        
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
        
        uint8_t buffer[1500];
        int packets_received = 0;
        
        for (int i = 0; i < max_packets * 100000; i++) {
            int len = 0;
            if (e1000_is_initialized()) {
                len = e1000_receive_packet(buffer, sizeof(buffer));
            } else if (ne2000_is_initialized()) {
                len = ne2000_receive_packet(buffer, sizeof(buffer));
            }
            if (len > 0) {
                packets_received++;
                printf("\n[Packet %d] Received %d bytes:\n", packets_received, len);
                
                // Process packet through network stack
                extern void netstack_process_packet(uint8_t *packet, uint16_t length);
                netstack_process_packet(buffer, len);
                
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
        uint8_t buffer[1500];
        int len = 0;
        
        if (e1000_is_initialized()) {
            len = e1000_receive_packet(buffer, sizeof(buffer));
        } else if (ne2000_is_initialized()) {
            len = ne2000_receive_packet(buffer, sizeof(buffer));
        } else {
            printf("Network card not initialized.\n");
            return;
        }
        
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

// Network stack commands
extern void netstack_set_config(uint32_t ip, uint32_t netmask, uint32_t gateway);
extern uint32_t parse_ipv4(const char *ip_string);
extern void netstack_process_packet(uint8_t *packet, uint16_t length);
extern void arp_send_request(uint32_t target_ip);

void cmd_ifconfig(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("IFCONFIG - Configure network interface\n");
        printf("Usage: ifconfig <ip> <netmask> <gateway>\n");
        printf("Example: ifconfig 10.0.2.15 255.255.255.0 10.0.2.1\n");
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
        printf("PING - Send ICMP echo request\n");
        printf("Usage: ping <ip_address>\n");
        printf("Example: ping 10.0.2.1\n");
        return;
    }
    
    uint32_t target_ip = parse_ipv4(arguments[0]);
    if (target_ip == 0) {
        printf("Error: Invalid IP address\n");
        return;
    }
    
    printf("PING %s...\n", arguments[0]);
    printf("Note: ICMP echo request not yet fully implemented\n");
    printf("Sending ARP request first...\n");
    arp_send_request(target_ip);
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

#include "userspace/bin/basic.c"

void cmd_basic(int arg_count, const char** arguments) {
    printf("\n=== BASIC Interpreter v1.2 ===\n");
    printf("Commands (case-insensitive):\n");
    printf("  RUN        - Execute the program\n");
    printf("  LIST       - Display program listing\n");
    printf("  NEW        - Clear program and variables\n");
    printf("  LOAD file  - Load .BAS file from filesystem\n");
    printf("  SAVE file  - Save program to .BAS file\n");
    printf("  HELP       - Show help\n");
    printf("  EXIT       - Return to shell\n");
    printf("\nEnter program lines with line numbers (0-99)\n");
    printf("Example: 10 PRINT \"HELLO\"\n\n");
    
    basic_interpreter(); // Call BASIC interpreter
    printf("\nReturned to shell.\n");
}
