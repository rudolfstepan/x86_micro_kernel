#include "command.h"
#include "prg.h"
#include "drivers/rtc/rtc.h"

#include "toolchain/strings.h"
#include "toolchain/stdio.h"


#define NUM_COMMANDS (sizeof(command_table) / sizeof(Command))


static char full_path[512] = "/"; // Full path including drive and path


// execute the program at the specified entry point
void call_program(long entryPoint) {
    void (*program)() = (void (*)())entryPoint;
    program(); // Jump to the program
}

// load the program into memory
void load_and_run_program(const char* programName) {
    // Load the program into the specified memory location
    if (openAndLoadFileToBuffer(programName, (void*)PROGRAM_LOAD_ADDRESS) > 0) {
        ProgramHeader* header = (ProgramHeader*)PROGRAM_LOAD_ADDRESS;
        // printf("Program Header Details:\n");
        // printf("Identifier: %s\n", header->identifier);
        // printf("Version: %u\n", header->version);
        // printf("Program size: %d\n", header->programSize);
        // printf("Entry point: %d\n", header->entryPoint);
        // printf("\n----------------------------------------------\n");
        call_program(header->entryPoint);
    } else {
        printf("%s not found\n", programName);
    }
}

void load_program(const char* programName) {
    // Load the program into the specified memory location
    if (openAndLoadFileToBuffer(programName, (void*)PROGRAM_LOAD_ADDRESS) > 0) {
        ProgramHeader* header = (ProgramHeader*)PROGRAM_LOAD_ADDRESS;
        printf("entryPoint: %X\n", header->entryPoint);
    } else {
        printf("%s not found\n", programName);
    }
}

// Print the prompt
void print_prompt() {
    // int year, month, day, hour, minute, second;
    // read_date(&year, &month, &day);
    // read_time(&hour, &minute, &second);
    // set_color(LIGHT_GREEN);
    // printf("%d-%d-%d %d:%d:%d", year, month, day, hour, minute, second);
    set_color(WHITE);
    
    //get_full_path(current_path, full_path, sizeof(full_path));
    printf("%s>", full_path);
}

void call_irq(int irq) {
    // Map IRQ number to the corresponding interrupt number
    // Assuming IRQs are mapped starting at interrupt number 0x20
    int int_number = irq + 0x20;

    switch (int_number) {
        case 0x20:
            __asm__ __volatile__("int $0x20");
            break;
        case 0x21:
            __asm__ __volatile__("int $0x21");
            break;
        // ... Handle other cases accordingly ...
        case 0x80:
            __asm__ __volatile__("int $0x80");
            break;
        default:
            // Handle invalid IRQ numbers
            break;
    }
}

// List the contents of the specified directory
void show_directory_content(const char *path) {
    unsigned int size = 1024;  // size is now an unsigned int, not a pointer
    char* buffer = (char*)malloc(size);

    if (readdir(path, buffer, &size) == 0) {  // Pass the address of size
        printf("Directory not found: %s\n", path);
    } else {
        printf("Listing directory: %s\n", path);
        printf("%s\n", buffer);
    }

    secure_free(buffer, size);  // Clear the buffer
}

// Open the specified file and print its contents
void openFile(const char* path) {
    printf("Opening file: %s\n", path);

    File* file = fopen(path, "r");
    if (file == NULL) {
        printf("File not found: %s\n", path);
        return;
    }

    printf("Name: %s\n", file->name);
    printf("Size: %d\n", file->size);

    char* buffer = (char*)malloc(sizeof(char) * file->size);
    if (buffer == NULL) {
        printf("Failed to allocate memory for file buffer\n");
        return;
    }

    // read the file into the buffer
    int result = fread(buffer, file->size, file);

    if (result == 0) {
        printf("Failed to read file\n");
        return;
    }

    printf("File contents:\n");
    printf("%s\n", buffer);

    secure_free(buffer, sizeof(buffer));  // Clear the buffer
}

// ---------------------------------------------------------------------------------------------
// Command handler functions
// ---------------------------------------------------------------------------------------------
void handle_mem(int arg_count, char **arguments) {
    print_memory_map(sys_mb_info);
}

void handle_dump(int arg_count, char **arguments) {
    uint32_t start_address = 0x80000000, end_address = 0x80000100;
    if (arg_count > 0) start_address = (uint32_t)strtoul(arguments[0], NULL, 16);
    if (arg_count > 1) end_address = (uint32_t)strtoul(arguments[1], NULL, 16);
    memory_dump(start_address, end_address);
}

void handle_cls(int arg_count, char **arguments) {
    clear_screen();
}

void handle_ls(int arg_count, char **arguments) {
    char full_path[512]; // Large enough to hold combined path
    // Generate the path based on arguments
    if (arg_count == 0) {
        snprintf(full_path, sizeof(full_path), "%s%s", current_drive, current_path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s%s/%s", current_drive, current_path, arguments[0]);
    }
    show_directory_content(full_path);
}

void handle_cd(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("CD command without arguments\n");
    } else {
        printf("Try changing directory to: %s\n", arguments[0]);
        printf("Current drive: %s\n", current_drive);
        printf("Current path: %s\n", current_path);
        snprintf(full_path, sizeof(full_path), "%s%s%s", current_drive, current_path, arguments[0]);
        printf("Full path: %s\n", full_path);
        printf("Try changing directory to: %s\n", full_path);
        if (chdir(arguments[0])) {
            //strncpy(current_path, normalized_path, MAX_PATH_LENGTH);
        } else {
            printf("Failed to change directory.\n");
        }
    }
}

void handle_help(int arg_count, char **arguments) {
    printf("Available commands: MEM, DUMP, CLS, LS, CD, ...\n");
}

void handle_mkdir(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("MKDIR command without arguments\n");
    } else {
        mkdir(arguments[0]);
    }
}

void handle_rmdir(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("RMDIR command without arguments\n");
    } else {
        rmdir(arguments[0]);
    }
}

void handle_mkfile(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("MKFILE command without arguments\n");
    } else {
        mkfile(arguments[0]);
    }
}

void handle_rmfile(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("RMFILE command without arguments\n");
    } else {
        rmfile(arguments[0]);
    }
}

void handle_run(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("RUN command without arguments\n");
    } else {

        char full_path[512]; // Large enough to hold combined path
        // Generate the path based on arguments
        snprintf(full_path, sizeof(full_path), "%s%s/%s", current_drive, current_path, arguments[0]);

        printf("Try Running program: %s\n", full_path);
        
        load_and_run_program(full_path);
    }
}

void handle_load(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("LOAD command without arguments\n");
    } else {
        load_program(arguments[0]);
    }
}

void handle_sys(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("SYS command without arguments\n");
    } else {
        long entryPoint = strtoul(arguments[0], NULL, 16);
        call_program(entryPoint);
    }
}

void handle_open(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("OPEN command without arguments\n");
    } else {
        openFile(arguments[0]);
    }
}

void handle_read_time(int arg_count, char **arguments) {
    int hour, minute, second;
    read_time(&hour, &minute, &second);
    printf("Time: %02d:%02d:%02d\n", hour, minute, second);
}

void handle_read_date(int arg_count, char **arguments) {
    int year, month, day;
    read_date(&year, &month, &day);
    printf("Date: %d-%02d-%02d\n", year, month, day);
}

void handle_set_time(int arg_count, char **arguments) {
    if (arg_count < 3) {
        printf("SETTIME command requires hour, minute, and second\n");
    } else {
        int hour = strtoul(arguments[0], NULL, 10);
        int minute = strtoul(arguments[1], NULL, 10);
        int second = strtoul(arguments[2], NULL, 10);
        write_time(hour, minute, second);
    }
}

void handle_set_date(int arg_count, char **arguments) {
    if (arg_count < 3) {
        printf("SETDATE command requires year, month, and day\n");
    } else {
        int year = strtoul(arguments[0], NULL, 10);
        int month = strtoul(arguments[1], NULL, 10);
        int day = strtoul(arguments[2], NULL, 10);
        write_date(year, month, day);
    }
}

void handle_irq(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("IRQ command without arguments\n");
    } else {
        int irq = strtoul(arguments[0], NULL, 10);
        printf("Handling IRQ %d\n", irq);
        call_irq(irq);
    }
}

// TODO: Implement sleep function
void handle_sleep(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("SLEEP command without arguments\n");
    } else {
        int seconds = strtoul(arguments[0], NULL, 10);
        printf("Sleeping for %d seconds\n", seconds);
        //sleep(seconds);
    }
}
// TODO: Implement exit function
void handle_exit(int arg_count, char **arguments) {
    printf("Exiting command interpreter\n");
    // Implement necessary cleanup and exit logic for the kernel or environment
    //exit(0);
}

// ---------------------------------------------------------------------------------------------
// Command table
// Add more commands as needed
// ---------------------------------------------------------------------------------------------
Command command_table[] = {
    {"MEM", handle_mem},
    {"DUMP", handle_dump},
    {"CLS", handle_cls},
    {"LS", handle_ls},
    {"CD", handle_cd},
    {"HELP", handle_help},
    {"DIR", handle_ls},
    {"MKDIR", handle_mkdir},
    {"RMDIR", handle_rmdir},
    {"MKFILE", handle_mkfile},
    {"RMFILE", handle_rmfile},
    {"RUN", handle_run},
    {"LOAD", handle_load},
    {"SYS", handle_sys},
    {"OPEN", handle_open},
    {"TIME", handle_read_time},
    {"DATE", handle_read_date},
    {"SETTIME", handle_set_time},
    {"SETDATE", handle_set_date},
    {"IRQ", handle_irq},
    {"SLEEP", handle_sleep},
    {"EXIT", handle_exit}
};

// ---------------------------------------------------------------------------------------------
// Command processor
// Split the input buffer into command and arguments
// will be called by the kernel when the Enter key is pressed by the user
// ---------------------------------------------------------------------------------------------
void process_command(char *input_buffer) {
    char command[32];
    char *arguments[10] = {0}; // Adjust size as needed
    int arg_count = split_input(input_buffer, command, arguments, 10, 50);

    if (command[0] == '\0') return;

    //printf("\n arguments address: %p, content: %s\n", (void*)arguments, arguments[0]);

    // go to the next line
    printf("\n");

    unsigned int i;
    for (i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(command, command_table[i].name) == 0) {
            command_table[i].handler(arg_count, (char **)arguments);
            break;
        }
    }

    if (change_drive(input_buffer)) {
        printf("Changed to drive: %s\n", current_drive);
    } else if (i == NUM_COMMANDS) {
        printf("Invalid command: %s\n", command);
    }
}
