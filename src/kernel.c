#include "strings.h"
#include "io.h"
#include "video.h"
#include "keyboard.h"
#include "fat32.h"
#include "prg.h"
#include "memory.h"
#include "system.h"
#include "sys.h"

// Assuming a maximum path length
#define MAX_PATH_LENGTH 256

#define PROGRAM_LOAD_ADDRESS 0x10000 // default address where the program will be loaded into memory except in the case of a program header
#define BUFFER_SIZE 256

#define PIT_FREQ 1193180
#define SPEAKER_PORT 0x61
#define PIT_CMD_PORT 0x43
#define PIT_CHANNEL_0 0x40

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

void beep(unsigned int freq) {
    // printf("Beep %u Hz\n", freq);
    unsigned int divisor = PIT_FREQ / freq;

    // Set PIT to square wave generator mode
    outb(PIT_CMD_PORT, 0b00110110);

    // Send the frequency divisor
    outb(PIT_CHANNEL_0, (unsigned char)(divisor & 0xFF)); // Send low byte
    outb(PIT_CHANNEL_0, (unsigned char)(divisor >> 8));   // Send high byte

    // Turn on the speaker
    unsigned char tmp = inb(SPEAKER_PORT) | 3;
    outb(SPEAKER_PORT, tmp);

    // Duration of beep - implement a delay or use a timer
    printf("Beep delay 5 begin\n");

    delay(5);

    // Turn off the speaker
    tmp = inb(SPEAKER_PORT) & 0xFC;
    outb(SPEAKER_PORT, tmp);

    printf("Beep finished\n");

    // int i=5;
    // vga_write_char(i/0);
}

// execute the program at the specified entry point
void executeProgram(long entryPoint) {
    void (*program)() = (void (*)())entryPoint;
    program(); // Jump to the program
}

// load the program into memory
void loadProgram(const char* programName) {
    // Load the program into the specified memory location
    if (openAndLoadFileToBuffer(programName, (void*)PROGRAM_LOAD_ADDRESS) > 0) {
        ProgramHeader* header = (ProgramHeader*)PROGRAM_LOAD_ADDRESS;
        // printf("Program Header Details:\n");
        // printf("Identifier: %s\n", header->identifier);
        // printf("Version: %u\n", header->version);
        // printf("Program size: %d\n", header->programSize);
        // printf("Entry point: %d\n", header->entryPoint);
        // printf("\n----------------------------------------------\n");
        executeProgram(header->entryPoint);
    } else {
        printf("Program not found\n");
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

    gdt_install();
    idt_install();
    isr_install();
    irq_install();
    __asm__ __volatile__("sti");
    timer_install();

    test_memory();

    init_fs(); // Initialize the file system

    initialize_syscall_table();

    kb_install();

    printf("+++++++++++++++++++++ Micro Kernel x86 ++++++++++++++++++++++++\n");
    printf("+                      Version 0.0.1                          +\n");
    printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

    printf("\n");
    printf("Commands: LS, CLS, CD [path], LOAD [Programm], MKDIR [name], RMDIR [name], MKFILE [name], RMFILE [name]\n");

    // show the prompt
    print_prompt();

    // char filesystemType[9]; // One extra byte for null-terminator
    // copy_and_terminate_string(filesystemType, boot_sector.fileSystemType, 8);
    // printf("File System Info:\n");
    // printf("Bytes per Sector: %u\n", boot_sector.bytesPerSector);
    // printf("Sectors per Cluster: %u\n", boot_sector.sectorsPerCluster);
    // printf("Filesystem Type: %s\n", filesystemType);
    // printf("Available Commands: dir\n");

    // printf("Size of uint32_t: %u bytes\n", sizeof(uint32_t));

    while (1) {

        // Manually clear the input buffer at the start of each iteration
        // for (int i = 0; i < BUFFER_SIZE; i++) {
        //     input_buffer[i] = '\0';
        // }
        // buffer_index = 0;

        // Get the first key press
        // unsigned char pressed_scancode = get_key_press();

        // Collect characters until Enter key is pressed
        // while (pressed_scancode != 0x1C) {  // 0x1C is the scancode for Enter key
        //     pressed_scancode = get_key_press();  // Get the key press
        //     if (pressed_scancode == BACKSPACE_PRESSED) {
        //         if (buffer_index > 0) {
        //             buffer_index--;             // Move back the buffer index
        //             input_buffer[buffer_index] = '\0';  // Remove the last character
        //             vga_backspace();            // Handle the backspace in your display (e.g., VGA)
        //         }
        //     } else {
        //         char ascii_char = scancode_to_ascii(pressed_scancode);  // Convert to ASCII
        //         if (ascii_char && buffer_index < BUFFER_SIZE - 1) {
        //             input_buffer[buffer_index++] = ascii_char; // Store the character
        //             vga_write_char(ascii_char);                // Echo the character
        //         }
        //     }
        // }

        // Check if Enter key is pressed
        if (enter_pressed) {

            // Process the command
            process_command(input_buffer);
            // Clear the flag and reset buffer_index
            enter_pressed = false;
            buffer_index = 0;

            // Clear the input buffer
            memset(input_buffer, 0, sizeof(input_buffer));

            // show the prompt
            print_prompt();
        }
    }

    return;
}

// Print the prompt
void print_prompt() {
    printf("\n%s>", current_path);
}

// Process the command
void process_command(char* input_buffer) {
    int arg_count = split_input(input_buffer, command, arguments, sizeof(input_buffer), 10);

    if (strcmp(command, "BEEP") == 0) {
        beep(5000); //  Hz
    } else if (strcmp(command, "CLS") == 0) {
        clear_screen();
    } else if (strcmp(command, "LS") == 0) {
        if (arg_count == 0) {
            read_directory_path(current_path);
        } else {
            read_directory_path(arguments[0]);
        }
    } else if (strcmp(command, "CD") == 0) {
        if (arg_count == 0) {
            printf("CD command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            strcpy(input_path, arguments[0]); // Replace this with actual user input
            // Normalize the input path
            normalize_path(input_path, normalized_path, current_path);

            if (change_directory(normalized_path)) {
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
            create_directory(arguments[0]);
        } else {
            printf("MKDIR command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "MKFILE") == 0) {
        if (arg_count == 0) {
            printf("MKFILE command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            create_file(arguments[0]);
        } else {
            printf("MKFILE command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "RMFILE") == 0) {
        if (arg_count == 0) {
            printf("RMFILE command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            delete_file(arguments[0]);
        } else {
            printf("RMFILE command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "RMDIR") == 0) {
        if (arg_count == 0) {
            // Handle 'RMDIR' without arguments
            printf("RMDIR command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            // Handle 'RMDIR'
            delete_directory(arguments[0]);
        } else {
            // Handle 'RMDIR' with unexpected arguments
            printf("RMDIR command with invalid or too many arguments\n");
        }
    } else if (strcmp(command, "LOAD") == 0) {
        if (arg_count == 0) {
            printf("LOAD command without arguments\n");
        } else if (arg_count == 1 && strlen(arguments[0]) > 0) {
            loadProgram(arguments[0]);
        } else {
            printf("LOAD command with invalid or too many arguments\n");
        }
    } else {
        printf("Invalid command: %s\n", command);
    }
}
