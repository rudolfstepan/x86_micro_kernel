#include "command.h"
#include "process.h"
#include "prg.h"
#include "sys.h"
#include "drivers/rtc/rtc.h"
#include "drivers/ata/ata.h"

#include "toolchain/strings.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "drivers/video/video.h"
#include "filesystem/fat32/fat32.h"
#include "filesystem/fat12/fat12.h"

#define NUM_COMMANDS (sizeof(command_table) / sizeof(Command))

typedef struct {
    const char *name;
    void (*handler)(int, char**);
} Command;


char current_path[256] = "/";

void handle_help();

// Open the specified file and print its contents
void openFile(const char* path) {
    printf("Opening file: %s\n", path);

    switch (current_drive->type) {
    case DRIVE_TYPE_ATA:

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

// ---------------------------------------------------------------------------------------------
// Command handler functions
// ---------------------------------------------------------------------------------------------
void handle_mem(int arg_count, char** arguments) {
    //print_memory_map(sys_mb_info);
}

void handle_dump(int arg_count, char** arguments) {
    uint32_t start_address = 0x80000000, end_address = 0x80000100;
    if (arg_count > 0) start_address = (uint32_t)strtoul(arguments[0], NULL, 16);
    if (arg_count > 1) end_address = (uint32_t)strtoul(arguments[1], NULL, 16);
    memory_dump(start_address, end_address);
}

void handle_cls(int arg_count, char** arguments) {
    clear_screen();
}

void handle_drives(int arg_count, char** arguments) {
    printf("Available drives:\n");
    list_detected_drives();
}

/// @brief Mount an attached drive
/// @param arg_count 
/// @param arguments 
void handle_mount(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("Mount command without arguments\n");
    } else {
        str_to_lower(arguments[0]);
        printf("Try mount drive: %s\n", arguments[0]);
        current_drive = get_drive_by_name(arguments[0]);
        if (current_drive == NULL) {
            printf("drive: %s not found\n", arguments[0]);
        } else {
            printf("Mounting drive\n");

            switch (current_drive->type) {
            case DRIVE_TYPE_ATA:
                printf("Init fs on ATA drive %s: %s with %u sectors\n", current_drive->name, current_drive->model, current_drive->sectors);
                // Initialize file system for ATA drive
                fat32_init_fs(current_drive->base, current_drive->is_master);
                strcpy(current_path, "/");
                break;
            case DRIVE_TYPE_FDD:
                printf("Init fs on FDD %s with CHS %u/%u/%u\n", current_drive->name, current_drive->cylinder, current_drive->head, current_drive->sector);
                // Initialize file system or handling code for FDD
                // Call fat12_init_fs as part of FDD initialization
                printf("Init fs on FDD drive %s\n", current_drive->name);
                fat12_init_fs(current_drive->fdd_drive_no);
                // reset the current path
                strcpy(current_path, "/");
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
void handle_ls(int arg_count, char** arguments) {
    const char* directory = (arg_count == 0) ? current_path : arguments[0];
    if (current_drive == NULL) {
        printf("No drive mounted\n");
        return;
    }
    switch (current_drive->type) {
    case DRIVE_TYPE_ATA:
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

void handle_cd(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("CD command without arguments\n");
    } else {
        str_trim_end(arguments[0], '/');  // Remove trailing slash from the path
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

void handle_mkdir(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("MKDIR command without arguments\n");
    } else {
        mkdir(arguments[0], 0);
    }
}

void handle_rmdir(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("RMDIR command without arguments\n");
    } else {
        rmdir(arguments[0]);
    }
}

void handle_mkfile(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("MKFILE command without arguments\n");
    } else {
        mkfile(arguments[0]);
    }
}

void handle_rmfile(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("RMFILE command without arguments\n");
    } else {
        remove(arguments[0]);
    }
}

void handle_load(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("LOAD command without arguments\n");
    } else {
        create_process(arguments[0]);
    }
}

TryContext ctx;

void handle_sys(int arg_count, char** arguments) {
    // if (arg_count == 0) {
    //     printf("SYS command without arguments\n");
    // } else {
    //     long entryPoint = strtoul(arguments[0], NULL, 16);
    //     start_program_execution(entryPoint);
    // }

    current_try_context = &ctx; // Set the global context pointer

    printf("Set context in user program: 0x%p\n", (void*)current_try_context);
    printf("Current ESP: 0x%X, ", get_esp());
    printf("EBP: 0x%X\n", get_ebp());

    if (setjmp(&ctx) == 0) {
        int x = 10;
        int y = 0;
        int z = x / y; // Trigger divide-by-zero exception
        printf("z = %d\n", z);
        printf("Try block executed successfully\n");
    } else {
        printf("Caught divide-by-zero exception\n");
    }

    current_try_context = NULL; // Clear the context pointer
    printf("Program execution continues...\n");
}




















void handle_open(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("OPEN command without arguments\n");
    } else {
        openFile(arguments[0]);
    }
}

void handle_read_datetime(int arg_count, char** arguments) {
    int hour, minute, second, year, month, day;
    read_time(&hour, &minute, &second);
    read_date(&year, &month, &day);

    printf("Current date and time: %d-%02d-%02d %02d:%02d:%02d\n", year, month, day, hour, minute, second);
}

void handle_set_time(int arg_count, char** arguments) {
    if (arg_count < 3) {
        printf("SETTIME command requires hour, minute, and second\n");
    } else {
        int hour = strtoul(arguments[0], NULL, 10);
        int minute = strtoul(arguments[1], NULL, 10);
        int second = strtoul(arguments[2], NULL, 10);
        write_time(hour, minute, second);
    }
}

void handle_set_date(int arg_count, char** arguments) {
    if (arg_count < 3) {
        printf("SETDATE command requires year, month, and day\n");
    } else {
        int year = strtoul(arguments[0], NULL, 10);
        int month = strtoul(arguments[1], NULL, 10);
        int day = strtoul(arguments[2], NULL, 10);
        write_date(year, month, day);
    }
}

void handle_irq(int arg_count, char** arguments) {
    int syscall_index = 0;  // Index of `kernel_hello`
    if (arg_count == 0) {

    } else {
        syscall_index = strtoul(arguments[0], NULL, 10);
    }

       //int syscall_index = 1;  // Syscall index
        int parameter = 1000;     // First argument
        int parameter1 = 20;    // Second argument
        int parameter2 = 30;    // Third argument
        int parameter3 = 40;    // Fourth argument

        __asm__ volatile(
            "int $0x80\n"       // Trigger syscall interrupt
            :
            : "a"(syscall_index), "b"(parameter), "c"(parameter1), "d"(parameter2), "e"(parameter3)
            : "memory"
        );

        printf("Return from syscall index: %d, Arguments: %d, %d, %d\n", syscall_index, parameter, parameter1, parameter2);
}

// TODO: Implement sleep function
void handle_sleep(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("SLEEP command without arguments\n");
    } else {
        int seconds = strtoul(arguments[0], NULL, 10);
        printf("Sleeping for %d seconds\n", seconds);
        sleep_ms(seconds * 1000);

        printf("Sleeping for %d seconds finished.\n", seconds);
    }
}
// TODO: Implement exit function
void handle_exit(int arg_count, char** arguments) {
    printf("Exiting command interpreter\n");
    // Implement necessary cleanup and exit logic for the kernel or environment
    exit(0);
}

void handle_fdd(int arg_count, char** arguments) {
    if (arguments[0] == NULL) {
        debug_read_bootsector(1);
    } else {

        int sector = strtoul(arguments[0], NULL, 10);

        printf("Reading sector %d\n", sector);

        debug_read_bootsector(sector);
    }
}

void handle_hdd(int arg_count, char** arguments) {
    if (arguments[0] == NULL) {
        ata_debug_bootsector(current_drive);
    } 
}

void handle_beep(int arg_count, char** arguments) {
    if (arg_count < 2) {
        //printf("BEEP command requires frequency and duration\n");
        beep(1000, 1000);
    } else {
        uint32_t frequency = strtoul(arguments[0], NULL, 10);
        uint32_t duration = strtoul(arguments[1], NULL, 10);
        beep(frequency, duration);
    }
}

void handle_wait(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("WAIT command without arguments\n");
    } else {
        int ticks = strtoul(arguments[0], NULL, 10);
        printf("Sleeping for %d ticks...\n", ticks);
        sleep_ms(ticks);
        printf("Done sleeping!\n");
    }
}

void handle_run(int arg_count, char** arguments) {
    if (arg_count == 0) {
        printf("RUN command without arguments\n");
        return;
    }
    char* program_name = arguments[0];
    
    int pid = create_process(program_name);
    if (pid == -1) {
        printf("Failed to start program '%s'.\n", program_name);
    }
}

// ---------------------------------------------------------------------------------------------
// Command table
// Add more commands as needed
// ---------------------------------------------------------------------------------------------
Command command_table[] = {
    {"HELP", handle_help},
    {"MEM", handle_mem},
    {"DUMP", handle_dump},
    {"CLS", handle_cls},
    {"LS", handle_ls},
    {"CD", handle_cd},
    {"DRIVES", handle_drives},
    {"MOUNT", handle_mount},
    {"MKDIR", handle_mkdir},
    {"RMDIR", handle_rmdir},
    {"MKFILE", handle_mkfile},
    {"RMFILE", handle_rmfile},
    {"RUN", handle_run},
    {"LOAD", handle_load},
    {"SYS", handle_sys},
    {"OPEN", handle_open},
    {"DATETIME", handle_read_datetime},
    {"SETTIME", handle_set_time},
    {"SETDATE", handle_set_date},
    {"IRQ", handle_irq},
    {"SLEEP", handle_sleep},
    {"EXIT", handle_exit},
    {"FDD", handle_fdd},
    {"HDD", handle_hdd},
    {"BEEP", handle_beep},
    {"WAIT", handle_wait},
    {"PID", list_running_processes}
};

// Function to handle the HELP command
void handle_help() {
    int num_commands = sizeof(command_table) / sizeof(command_table[0]);
    printf("Available commands:\n");
    for (int i = 0; i < num_commands; i++) {
        printf(" %s ", command_table[i].name);
    }
    printf("\n");
}

#define MAX_INPUT_LENGTH 256 // Define a maximum length for the input buffer

bool is_null_terminated(char* buffer, size_t max_length) {
    for (size_t i = 0; i < max_length; i++) {
        if (buffer[i] == '\0') {
            return true; // Found the null terminator
        }
    }
    return false; // No null terminator found within max_length
}

// ---------------------------------------------------------------------------------------------
// Command processor
// Split the input buffer into command and arguments
// will be called by the kernel when the Enter key is pressed by the user
// ---------------------------------------------------------------------------------------------
void process_command(char* input_buffer) {
    char command[32];
    char* arguments[10] = { 0 }; // Adjust size as needed

    // Check if the input buffer is null-terminated within the allowed length
    if (!is_null_terminated(input_buffer, MAX_INPUT_LENGTH)) {
        // Handle error: input buffer is not null-terminated
        printf("Error: input buffer is not null-terminated.\n");
        return;
    }

    // check termination of the input buffer
    if (input_buffer[0] == '\0') return;

    // Parse command and arguments (simple parsing logic)
    size_t i = 0;
    size_t cmd_len = 0;

    // Extract command until space or null terminator is reached
    while (input_buffer[i] != ' ' && input_buffer[i] != '\0' && cmd_len < sizeof(command) - 1) {
        command[cmd_len++] = input_buffer[i++];
    }
    command[cmd_len] = '\0'; // Null-terminate the command

    // Skip any spaces before arguments
    while (input_buffer[i] == ' ') {
        i++;
    }

    // Process arguments (split by spaces)
    size_t arg_count = 0;
    while (input_buffer[i] != '\0' && arg_count < 10) {
        arguments[arg_count++] = &input_buffer[i];
        while (input_buffer[i] != ' ' && input_buffer[i] != '\0') {
            i++;
        }
        if (input_buffer[i] == ' ') {
            input_buffer[i++] = '\0'; // Replace space with null terminator to split arguments
        }
    }

    // Print the parsed command and arguments for verification
    // printf("Command: %s\n", command);
    // for (size_t j = 0; j < arg_count; j++) {
    //     printf("Argument %u: %s\n", j, arguments[j]);
    // }

    if (command[0] == '\0') return;

    // go to the next line
    printf("\n");

    unsigned int ii;
    for (ii = 0; ii < NUM_COMMANDS; ii++) {
        if (strcmp(command, command_table[ii].name) == 0) {
            command_table[ii].handler(arg_count, (char**)arguments);
            break;
        }
    }

    // if the command is not found
    if (ii == NUM_COMMANDS) {
        char* program = strcat(command, ".PRG");
        load_and_execute_program(program);
    }
}
