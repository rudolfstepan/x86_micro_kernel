#include "command.h"
#include "prg.h"
#include "drivers/rtc/rtc.h"

#include "toolchain/strings.h"
#include "toolchain/stdio.h"


#define NUM_COMMANDS (sizeof(command_table) / sizeof(Command))


char current_path[256] = "/";

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
    printf("%s>", current_path);
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

void handle_drives(int arg_count, char **arguments) {
    printf("Available drives:\n");
    list_detected_drives();
}

/// @brief Mount an attached drive
/// @param arg_count 
/// @param arguments 
void handle_mount(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("Mount command without arguments\n");
    } else {
        str_to_lower(arguments[0]);
        printf("Try mount drive: %s\n", arguments[0]);
        current_drive = get_drive_by_name(arguments[0]);
        if(current_drive == NULL){
            printf("drive: %s not found\n", arguments[0]);
        }else{
            printf("Mounting drive \n");

            switch (current_drive->type)
            {
                case DRIVE_TYPE_ATA:
                    printf("Init fs on ATA drive %s: %s with %u sectors\n", current_drive->name, current_drive->model, current_drive->sectors);
                    // Initialize file system for ATA drive
                    fat32_init_fs(current_drive->base, current_drive->is_master);
                    break;
                case DRIVE_TYPE_FDD:
                    printf("Init fs on FDD %s with CHS %u/%u/%u\n", current_drive->name, current_drive->cylinder, current_drive->head, current_drive->sector);
                    // Initialize file system or handling code for FDD
                    // Call fat12_init_fs as part of FDD initialization
                    if (fat12_init_fs()) {
                        printf("FAT12 file system initialized successfully.\n");
                    } else {
                        printf("FAT12 initialization failed.\n");
                    }
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
void handle_ls(int arg_count, char **arguments) {
    unsigned int size = 1024;  
    char *buffer = (char *)malloc(size);
    if (!buffer) {
        printf("Failed to allocate memory\n");
        return;
    }
    const char *directory = (arg_count == 0) ? current_path : arguments[0];
    if(current_drive == NULL){
        printf("No drive mounted\n");
        return;
    }
    readdir(directory, buffer, &size, current_drive->type);

    printf("%s\n", buffer);
    memset(buffer, 0, size);  // Securely clear the buffer
    free(buffer);
    buffer = NULL;
}

void handle_cd(int arg_count, char **arguments) {
    if (arg_count == 0) {
        printf("CD command without arguments\n");
    } else {
        char new_path[256] = "/";
        snprintf(new_path, sizeof(current_path), "%s%s/", current_path, arguments[0]);
        if(change_directory(new_path)){
            strcpy(current_path, new_path);
            printf("Set directory to: %s\n", arguments[0]);
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
        load_and_run_program(arguments[0]);
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

void handle_fdd(int arg_count, char **arguments) {

    if(arguments[0] == NULL){
        debug_read_bootsector(1);
    }else{

        int sector = strtoul(arguments[0], NULL, 10);

        printf("Reading sector %d\n", sector);

        debug_read_bootsector(sector);
    }
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
    {"DRIVES", handle_drives},
    {"MOUNT", handle_mount},
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
    {"EXIT", handle_exit},
    {"FDD", handle_fdd}
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
}
