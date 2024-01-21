#include "strings.h"
#include "io.h"
#include "video.h"
#include "keyboard.h"
#include "fat32.h"
#include "prg.h"
#include "system.h"
#include "sys.h"
#include "cmos.h"

#include "stdbool.h"

// custom implementations
#include "stdio.h"
#include "stdlib.h"

// Assuming a maximum path length
#define MAX_PATH_LENGTH 256

#define PROGRAM_LOAD_ADDRESS 0x10000 // default address where the program will be loaded into memory except in the case of a program header
#define BUFFER_SIZE 256

#define PIT_FREQ 1193180
#define SPEAKER_PORT 0x61
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL_0 0x40

// for memory dump
#define BYTES_PER_LINE 16
#define MAX_LINES 20

char input_path[MAX_PATH_LENGTH];      // This should be set to the user input
char normalized_path[MAX_PATH_LENGTH]; // This should be set to the normalized path
char command[50], arguments[10][50];   // Adjust sizes as necessary

// for the keyboard
extern char input_buffer[BUFFER_SIZE];
extern volatile int buffer_index;
extern volatile bool enter_pressed;

char current_path[MAX_PATH_LENGTH] = "/"; // Initial path

void print_prompt();
void process_command(char* command);
void list_directory(const char* path);
void openFile(const char* path);
void memory_dump(uint32_t start_address, uint32_t end_address);


void beep(unsigned int freq) {
    printf("Beep %u Hz\n", freq);
    // unsigned int divisor = PIT_FREQ / freq;

    // // Set PIT to square wave generator mode
    // outb(PIT_CMD_PORT, 0b00110110);

    // // // Send the frequency divisor
    // // outb(PIT_CHANNEL_0, (unsigned char)(divisor & 0xFF)); // Send low byte
    // // outb(PIT_CHANNEL_0, (unsigned char)(divisor >> 8));   // Send high byte

    // Turn on the speaker
    // unsigned char tmp = inb(SPEAKER_PORT) | 3;
    // outb(SPEAKER_PORT, tmp);

    // Duration of beep - implement a delay or use a timer
    printf("Beep delay 5 begin\n");

    delay(5);

    // Turn off the speaker
    // tmp = inb(SPEAKER_PORT) & 0xFC;
    // outb(SPEAKER_PORT, tmp);

    printf("Beep finished\n");

    // int i=5;
    // vga_write_char(i/0);
}

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

// Function to normalize a file path.
void normalize_path(char* input_path, char* normalized_path, const char* current_path) {
    if (input_path[0] == '/') {
        // Absolute path, copy it directly.
        strncpy(normalized_path, input_path, MAX_PATH_LENGTH - 1);
    } else {
        // Relative path, check if the current path is root "/"
        if (strcmp(current_path, "/") == 0) {
            // If current path is root, concatenate without adding an extra slash
            snprintf(normalized_path, MAX_PATH_LENGTH, "/%s", input_path);
        } else {
            // Otherwise, concatenate normally
            snprintf(normalized_path, MAX_PATH_LENGTH, "%s/%s", current_path, input_path);
        }
    }
    normalized_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null termination
}

void main(void) {

    initializeHeap();
    test_memory();

    gdt_install();
    idt_install();
    isr_install();
    irq_install();
    __asm__ __volatile__("sti");
    timer_install();

    init_fs(); // Initialize the file system

    //initialize_syscall_table();

    kb_install();

    printf("++++++++++++++++++++++++++++ Mini OS ++++++++++++++++++++++++++\n");
    printf("+                        Version 0.0.1                        +\n");
    printf("+                        2021-05-01                           +\n");
    printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

    printf(" HELP for available commands.\n");

    int year, month, day, hour, minute, second;
    getDate(&year, &month, &day);
    getTime(&hour, &minute, &second);

    printf("Date Time: %d-%d-%d %d:%d:%d\n", year, month, day, hour, minute, second);

    // show the prompt
    print_prompt();

    while (1) {
        // Check if Enter key is pressed
        if (enter_pressed) {
            // Process the command
            process_command(input_buffer);
            // Clear the flag and reset buffer_index
            enter_pressed = false;
            buffer_index = 0;
            // Clear the input buffer
            memset(input_buffer, 0, sizeof(input_buffer));
        }
    }

    return;
}

// Print the prompt
void print_prompt() {
    printf("%s>", current_path);
}

// Split the input string into command and arguments
// Process the command
// Print the prompt
void process_command(char* input_buffer) {
    int arg_count = split_input(input_buffer, command, arguments, sizeof(input_buffer), 10);

    if(input_buffer[0] == 0) {
        return;
    }

    printf("\n");

    // printf("Command: %s\n", command);
    // printf("Arguments: %d\n", arg_count);
    // for (int i = 0; i < arg_count; i++) {
    //     printf("Argument %d: %s\n", i, arguments[i]);
    // }

    if (strcmp(command, "BEEP") == 0) {
        beep(5000); //  Hz
    } else if (strcmp(command, "DUMP") == 0) {
        if (arg_count == 0) {
            //printf("DUMP command without arguments\n");
            memory_dump(0x80000000, 0x80000100);
        } else if (arg_count > 0 && strlen(arguments[0]) > 0) {
            // Convert string arguments to addresses
            uint32_t start_address = (uint32_t)strtoul(arguments[0], NULL, 16);
            uint32_t end_address = 0;
            // Check if there is a second argument
            if(strlen(arguments[0]) > 0){
                end_address = (uint32_t)strtoul(arguments[1], NULL, 16);
            } 
            // Call memory_dump with the converted addresses
            memory_dump(start_address, end_address);
        } else {
            printf("DUMP command with invalid or too many arguments\n");
        } 
    } else if (strcmp(command, "CLS") == 0) {
        clear_screen();
    } else if (strcmp(command, "LS") == 0) {
        if (arg_count == 0) {
            list_directory(current_path);
        } else {
            list_directory(arguments[0]);
        }
    } else if (strcmp(command, "CD") == 0) {
        if (arg_count == 0) {
            printf("CD command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            strcpy(input_path, arguments[0]); // Replace this with actual user input
            // Normalize the input path
            normalize_path(input_path, normalized_path, current_path);

            if (chdir(normalized_path)) {
                // If directory change is successful, update current_path
                strncpy(current_path, normalized_path, MAX_PATH_LENGTH);
                current_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null termination
            } else {
                printf("Failed to change directory.\n");
            }
            // printf("Current path: %s\n", current_path);
        } else {
            printf("CD command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "MKDIR") == 0) {
        if (arg_count == 0) {
            printf("MKDIR command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            mkdir(arguments[0]);
        } else {
            printf("MKDIR command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "MKFILE") == 0) {
        if (arg_count == 0) {
            printf("MKFILE command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            mkfile(arguments[0]);
        } else {
            printf("MKFILE command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "RMFILE") == 0) {
        if (arg_count == 0) {
            printf("RMFILE command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            rmfile(arguments[0]);
        } else {
            printf("RMFILE command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "RMDIR") == 0) {
        if (arg_count == 0) {
            // Handle 'RMDIR' without arguments
            printf("RMDIR command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            // Handle 'RMDIR'
            rmdir(arguments[0]);
        } else {
            // Handle 'RMDIR' with unexpected arguments
            printf("RMDIR command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "RUN") == 0) {
        if (arg_count == 0) {
            printf("RUN command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            load_and_run_program(arguments[0]);
        } else {
            printf("RUN command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "LOAD") == 0) {
        if (arg_count == 0) {
            printf("LOAD command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            load_program(arguments[0]);
        } else {
            printf("LOAD command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "SYS") == 0) {
        if (arg_count == 0) {
            printf("SYS command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            // Convert string argument to address
            long entryPoint = (long)strtoul(arguments[0], NULL, 16);
            call_program(entryPoint);
        } else {
            printf("SYS command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "OPEN") == 0) {
        if (arg_count == 0) {
            printf("OPEN command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            openFile(arguments[0]);
        } else {
            printf("OPEN command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "HELP") == 0) {
            printf("LS, CLS, CD [path]\n");
            printf("MKDIR [name], RMDIR [name]\n");
            printf("MKFILE [name], RMFILE [name]\n");
            printf("RUN [Programm], LOAD [Programm], SYS [address], OPEN [file]\n");
    } else {
        printf("Invalid command: %s\n", command);
    }

    // show the prompt
    print_prompt();
}

// List the contents of the specified directory
void list_directory(const char* path) {
    printf("Listing directory: %s\n", path);

    unsigned int size = 1024;  // size is now an unsigned int, not a pointer
    char* buffer = (char*)malloc(size);

    if (readdir(path, buffer, &size) == 0) {  // Pass the address of size
        printf("Directory not found: %s\n", path);
    } else {
        printf("%s\n", buffer);
    }

    secure_free(buffer, size);  // Clear the buffer
}

// Open the specified file and print its contents
void openFile(const char* path) {
    printf("Opening file: %s\n", path);

    File* file = fopen(path, "r");
    if(file == NULL) {
        printf("File not found: %s\n", path);
        return;
    }

    printf("Name: %s\n", file->name);
    printf("Size: %d\n", file->size);

    char* buffer = (char*)malloc(sizeof(char) * file->size);
    if(buffer == NULL) {
        printf("Failed to allocate memory for file buffer\n");
        return;
    }

    // read the file into the buffer
    int result = fread(buffer, file->size, file);

    if(result == 0) {
        printf("Failed to read file\n");
        return;
    }

    printf("File contents:\n");
    printf("%s\n", buffer);


    secure_free(buffer, sizeof(buffer));  // Clear the buffer
}

// Check if a character is printable
int is_printable(char ch) {
    return (ch >= 32 && ch < 127);
}

// Convert a byte to a printable character or '.'
char to_printable_char(char ch) {
    return is_printable(ch) ? ch : '.';
}

// // Simple function to wait for Enter key
// void wait_for_enter() {
//     printf("Press Enter to continue...");
//     while (getchar() != '\n');
// }

// Memory dump function
void memory_dump(uint32_t start_address, uint32_t end_address) {
    if (end_address == 0) {
        end_address = start_address + (BYTES_PER_LINE * MAX_LINES); // Default length for 20 lines
    }

    uint8_t* ptr = (uint8_t*)start_address;
    int line_count = 0;

    while (ptr < (uint8_t*)end_address) {
        printf("%08X: ", (unsigned int)(uintptr_t)ptr);

        // Print each byte in hex and store ASCII characters
        char ascii[BYTES_PER_LINE + 1];

        for (int i = 0; i < BYTES_PER_LINE; ++i) {
            if (ptr + i < (uint8_t*)end_address) {
                printf("%02X ", ptr[i]);
                ascii[i] = is_printable(ptr[i]) ? ptr[i] : '.';
            } else {
                printf("   ");
                ascii[i] = ' ';
            }
        }

        printf(" |%s|\n", ascii);
        ptr += BYTES_PER_LINE;
        line_count++;

        if (line_count >= MAX_LINES) {
            wait_for_enter(); // Wait for user to press Enter
            line_count = 0;   // Reset line count
        }
    }
}