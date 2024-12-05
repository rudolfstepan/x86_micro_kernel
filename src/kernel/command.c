#include "command.h"
#include "process.h"
#include "prg.h"
#include "sys.h"
#include "scheduler.h"
#include "memory.h"

#include "drivers/rtc/rtc.h"
#include "drivers/ata/ata.h"

#include "toolchain/string.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "drivers/video/video.h"
#include "drivers/kb/kb.h"
#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "filesystem/fat12/fat12.h"
#include "drivers/network/rtl8139.h"
#include "drivers/network/e1000.h"
#include "drivers/network/ne2000.h"
#include "drivers/network/vmxnet3.h"


char current_path[256] = "/";

// Splits an input string into a command and arguments.
int split_input(const char* input, char* command, char** arguments, int max_length, int max_args);
void openFile(const char* path);

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
    {NULL, NULL} // End marker
};

#define MAX_ARGS 10
#define MAX_LENGTH 64


void process_command(char *input_buffer) {
    char command[MAX_LENGTH];
    char* arguments[MAX_ARGS];
    int arg_cnt = split_input(input_buffer, command, arguments, MAX_LENGTH, MAX_ARGS);

    // Check if the command is empty
    if (command[0] == '\0') {
        return; // Empty input
    }

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

    asm volatile("int $0x29"); // Trigger a timer interrupt
}

void command_loop() {
    int counter = 0;
    printf("+++command_loop started\n");
    printf("> ");

    char* input = (char*)k_malloc(128);
    if (input == NULL) {
        printf("Failed to allocate memory for input buffer\n");
        return;
    }

    int buffer_index = 0;

    while (1) {
        // Get keyboard input
        char ch = input_queue_pop();
        if (ch != 0) {
            if (ch == '\n') {
                // Uppercase the input
                input[buffer_index] = '\0'; // Null-terminate
                str_to_upper(input);
                buffer_index = 0;
                printf("\n");
                // Call the command interpreter
                process_command(input);
                printf("> ");
            }else if (ch == '\b') {
                if (buffer_index > 0) {
                    buffer_index--;
                    vga_backspace();
                }
            } else {
                // Regular character handling
                input[buffer_index++] = ch;
                putchar(ch); // Echo the character to the screen
            }
        }

        asm volatile("int $0x29"); // Trigger a timer interrupt
    }
}

// Splits an input string into a command and arguments
int split_input(const char* input, char* command, char** arguments, int max_args, int max_length) {
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
            // Return the number of successfully parsed arguments
            return arg_count;
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
    for (int i = 0; i < arg_count; i++) {
        free(arguments[i]);
    }
}

// Command implementations
void cmd_help(int arg_count, const char **args) {
    printf("Available commands:\n");
    for (int i = 0; command_table[i].name != NULL; i++) {
        printf(" - %s\n", command_table[i].name);
    }
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
    } else {
        str_to_lower((char *)arguments[0]);
        printf("Try mount drive: %s\n", arguments[0]);
        current_drive = get_drive_by_name(arguments[0]);
        if (current_drive == NULL) {
            printf("drive: %s not found\n", arguments[0]);
        } else {
            printf("Mounting drive\n");

            switch (current_drive->type) {
            case DRIVE_TYPE_ATA:
                //printf("Init fs on ATA drive %s: %s with %u sectors\n", current_drive->name, current_drive->model, current_drive->sectors);
                // Initialize file system for ATA drive
                init_fs(current_drive);
                // strcpy(current_path, "/");
                break;
            case DRIVE_TYPE_FDD:
                //printf("Init fs on FDD %s with CHS %u/%u/%u\n", current_drive->name, current_drive->cylinder, current_drive->head, current_drive->sector);
                // Initialize file system or handling code for FDD
                // Call fat12_init_fs as part of FDD initialization
                printf("Init fs on FDD drive %s\n", current_drive->name);
                fat12_init_fs(current_drive->fdd_drive_no);
                // reset the current path
                //strcpy(current_path, "/");
                break;

            default:
                break;
            }

            strcpy(current_path, "/");
        }
    }
}

/// @brief List the directory content
/// @param arg_count 
/// @param arguments 
void cmd_ls(int arg_count, const char** arguments) {
    const char* directory = (arg_count == 0) ? current_path : arguments[0];
    if (current_drive == NULL) {
        printf("No drive mounted\n");
        return;
    }
    switch (current_drive->type) {
    case DRIVE_TYPE_ATA:

        // extern fat32_class_t fat32;
        // ctor_fat32_class(&fat32);
        // fat32.fat32_read_dir(directory);
        fat32_read_dir(directory);

        break;
    case DRIVE_TYPE_FDD:
        // notice that the path is relative to the current directory
        // we only need the filename
        fat12_read_dir(arguments[0]);
        break;

    default:
        break;
    }
}

void cmd_cd(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("CD command without arguments\n");
    } else {
        str_trim_end((char*)arguments[0], '/');  // Remove trailing slash from the path
        char new_path[256] = "/";
        snprintf(new_path, sizeof(current_path), "%s/%s", current_path, arguments[0]);

        if (current_drive == NULL) {
            printf("No drive mounted\n");
            return;
        }

        if (current_drive->type == DRIVE_TYPE_ATA) {
            if (fat32_change_directory(new_path)) {
                strcpy(current_path, new_path);
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
                strcpy(current_path, new_path);
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
        openFile(arguments[0]);
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

        int sector = strtoul(arguments[0], NULL, 10);

        printf("Reading sector %d\n", sector);

        debug_read_bootsector(sector);
    }
}

void cmd_hdd(int arg_count, const char** arguments) {
    if (arguments[0] == NULL) {
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
void openFile(const char* path) {
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
        printf("Size: %d\n", file->size);

        // Calculate the size of the buffer based on the size of the file
        size_t bufferSize = file->size; // Use the file size as the buffer size directly

        // Allocate the buffer
        char* buffer = (char*)malloc(bufferSize +1);
        if (buffer == NULL) {
            printf("Failed to allocate memory for file buffer\n");
            return;
        }

        // Read the file into the buffer, passing the correct size
        int result = fat32_read_file(file, buffer, bufferSize, bufferSize); // Pass bufferSize as the buffer size
        if (result == 0) {
            printf("Failed to read file\n");
            return;
        }

        printf("Result: %d\n", result);

        printf("File contents:\n");
        printf("%s\n", buffer);

        secure_free(buffer, sizeof(buffer));  // Clear the buffer
        break;
}
    case DRIVE_TYPE_FDD:
    {
        // TODO: use the gerneric FILE structure to read the file
        Fat12File* file = fat12_open_file(path, "r");
        if (file == NULL) {
            printf("File not found: %s\n", path);
            return;
        }

        // Calculate the size of the buffer based on the size of the file
        size_t bufferSize = file->size; // Use the file size as the buffer size directly

        // Allocate the buffer
        char* buffer = (char*)malloc(sizeof(char) * bufferSize);
        if (buffer == NULL) {
            printf("Failed to allocate memory for file buffer\n");
            return;
        }

        // Read the file into the buffer, passing the correct size
        int result = fat12_read_file(file, buffer, bufferSize, file->size); // Pass bufferSize as the buffer size
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

    int taskId = strtoul(arguments[0], NULL, 10);
    if (taskId < 0 || taskId >= MAX_TASKS) {
        printf("Invalid task ID: %d\n", taskId);
        return;
    }

    //start_task(taskId);
}

void cmd_net(int arg_count, const char** arguments) {
    if (arg_count == 0) {
        printf("NET command without arguments\n");
        return;
    }

    if (strcmp(arguments[0], "LIST") == 0) {
        // List network interfaces
        //list_network_interfaces();
    } else if (strcmp(arguments[0], "INFO") == 0) {
        // Get network interface information
        //get_network_interface_info();
    } else if(strcmp(arguments[0], "SEND") == 0) {
        // Send a packet
        //send_packet();

        //test_loopback();
        //e1000_send_test_packet();
        ne2000_test_send();
        //test_vmxnet3();
        //rtl8139_send_test_packet();


    } else if(strcmp(arguments[0], "RECV") == 0) {
        // Receive a packet
        //receive_packet();
    }
    
    else {
        printf("Unknown NET command: %s\n", arguments[0]);
    }
}