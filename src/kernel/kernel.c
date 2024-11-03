#include <stdbool.h>

#include "command.h"
#include "prg.h"
#include "system.h"
#include "sys.h"

#include "drivers/keyboard/keyboard.h"
#include "drivers/rtc/rtc.h"
#include "drivers/video/video.h"
#include "drivers/io/io.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

#include "filesystem/filesystem.h"
#include "drivers/ata/ata.h"

// for the keyboard
extern char input_buffer[BUFFER_SIZE];
extern volatile int buffer_index;
extern volatile bool enter_pressed;

//---------------------------------------------------------------------------------------------
// syscall table entry points and definitions
//---------------------------------------------------------------------------------------------
typedef void (*syscall_func_ptr)(void);
typedef void (*syscall_sleep_func_ptr)(int);
// Use a specific section for the syscall table to allow the linker to control the address
syscall_func_ptr syscall_table[NUM_SYSCALLS] __attribute__((section(".syscall_table")));

void syscall_sleep(int ticks) {
    if (ticks > 100) {
        ticks = 1;
    }
    // Code to handle passing arguments to the system call...
    printf("Sleeping for %u ticks...\n", ticks);
    //delay(ticks);
    printf("Done sleeping!\n");
}
//---------------------------------------------------------------------------------------------
__attribute__((naked)) void syscall_handler() {
    __asm__ volatile(
        "pusha\n"                          // Push all general-purpose registers

        "cmp $0, %%eax\n"                  // Compare EAX with 0
        "jne not_get_address\n"            // If not equal, jump to other syscall handling
        "movl syscall_table_address, %%eax\n" // Move the address of the syscall table into EAX
        "jmp done_handling\n"              // Jump to the end of the handler

        "not_get_address:\n"
        // Add more comparisons and jumps here for other syscalls

        "done_handling:\n"
        "popa\n"                           // Pop all general-purpose registers
        "iret\n"                           // Return from interrupt
        :
        :
        : "memory"
        );
}

// This would be your syscall_table_address variable, initialized somewhere in your kernel startup code
uintptr_t syscall_table_address = (uintptr_t)&syscall_table;

// Initialize the syscall table
void initialize_syscall_table() {
    // No need to set the address here; the linker will take care of it
    syscall_table[SYSCALL_SLEEP] = (syscall_func_ptr)&syscall_sleep;

    irq_install_handler(128, syscall_handler); // Install the system call handler
    //__asm__ __volatile__("int $0x80"); // Invoke interrupt 0x80
}

// initialize attaches drives
void init_drives()
{
    // detect and initialize attached drives to the system
    ata_detect_drives();

    // current_drive = ata_get_drive(0);

    // if (current_drive) {
    //     printf("Drive %s found: %s, Sectors: %u\n", current_drive->name, current_drive->model, current_drive->sectors);
    //     init_fs(current_drive);
    // } else {
    //     printf("Drive not found.\n");
    // }

    // detect fdd drives
    fdd_detect_drives();
}

// ---------------------------------------------------------------------------------------------
// main routine of the kernel
// This is the entry point of the kernel
// ---------------------------------------------------------------------------------------------
void main(uint32_t multiboot_magic, MultibootInfo* mb_info) {
    sys_mb_info = mb_info;
    // Check if the magic number is correct
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        printf("Invalid magic number: 0x%x\n", multiboot_magic);
        return;
    }
    initialize_memory_system();
    gdt_install();
    idt_install();
    isr_install();
    irq_install();
    __asm__ __volatile__("sti");
    
    // syscall table
    initialize_syscall_table();
    kb_install();
    set_color(WHITE);

    printf("===============================================================================\n");
    printf("|                 x86 Micro Kernel written by Rudolf Stepan 2024              |\n");
    printf("===============================================================================\n");

    init_drives();

    printf("Type HELP for command list.\n");

    test_memory();
    timer_install();
    // test the colors
    set_color(BLACK); printf("Black ");
    set_color(BLUE); printf("Blue ");
    set_color(GREEN); printf("Green ");
    set_color(CYAN); printf("Cyan ");
    set_color(RED); printf("Red ");
    set_color(MAGENTA); printf("Magenta ");
    set_color(BROWN); printf("Brown ");
    set_color(LIGHT_GRAY); printf("Light Grey ");
    set_color(DARK_GRAY); printf("Dark Grey ");
    set_color(LIGHT_BLUE); printf("Light Blue ");
    set_color(LIGHT_GREEN); printf("Light Green ");
    set_color(LIGHT_CYAN); printf("Light Cyan ");
    set_color(LIGHT_RED); printf("Light Red ");
    set_color(LIGHT_MAGENTA); printf("Light Magenta ");
    set_color(YELLOW); printf("Yellow ");
    set_color(WHITE); printf("White\n");
    set_color(WHITE);
    // show the prompt
    print_prompt();

    // Main loop
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

            // Show the prompt
            print_prompt();
        }
    }

    return;
}
