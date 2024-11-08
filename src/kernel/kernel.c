#include <stdbool.h>

#include "command.h"
#include "prg.h"
#include "system.h"
#include "sys.h"
#include "pit.h"

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
    // __asm__ __volatile__("int $0x26"); // Invoke interrupt
}

// initialize attaches drives
void init_drives()
{
    // detect and initialize attached drives to the system
    ata_detect_drives();

    current_drive = ata_get_drive(0);

    if (current_drive) {
        printf("Drive %s found: %s, Sectors: %u\n", current_drive->name, current_drive->model, current_drive->sectors);
        init_fs(current_drive);
    } else {
        printf("Drive not found.\n");
    }

    // detect fdd drives
    fdd_detect_drives();
}

void display_welcome_message() {
    set_color(LIGHT_MAGENTA);
    printf("\n");
    printf("      *------------------------------------------------------------*\n");
    printf("      |        Welcome to the Rudolf Stepan x86 Micro Kernel       |\n");
    printf("      |      Type 'HELP' for a list of commands and instructions   |\n");
    printf("      *------------------------------------------------------------*\n");
    printf("\n");
    set_color(WHITE);
}

void display_color_test() {
    printf("\nColor Test: ");
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
    set_color(WHITE); printf("White\n\n");
    set_color(WHITE);
}

void print_fancy_prompt() {
    set_color(GREEN);
    str_trim_end(current_path, '/');
    printf("%s%s>", current_drive->name, current_path);
    set_color(WHITE);
}

void main(uint32_t multiboot_magic, MultibootInfo* mb_info) {
    sys_mb_info = mb_info;
    // Check if the magic number is correct
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        set_color(RED);
        printf("Invalid magic number: 0x%x\n", multiboot_magic);
        set_color(WHITE);
        return;
    }

    initialize_memory_system();
    gdt_install();
    idt_install();
    isr_install();
    irq_install();
    initialize_syscall_table();
    // Install the IRQ handler for the keyboard
    irq_install_handler(1, kb_handler);
    // Install the IRQ handler for the FDD
    irq_install_handler(6, fdd_irq_handler);
    // Install the IRQ handler for the timer
    //irq_install_handler(0, timer_irq_handler);

    __asm__ __volatile__("sti"); // enable interrupts

    // display_kernel_banner();
    // display_ascii_art();
    display_welcome_message();
    init_drives();
    test_memory();
    kb_install();
    printf("Type HELP for command list.\n");

    display_color_test();
    print_fancy_prompt();

    // Main loop
    while (1) {
        if (enter_pressed) {
            process_command(input_buffer);
            enter_pressed = false;
            buffer_index = 0;
            memset(input_buffer, 0, sizeof(input_buffer));
            print_fancy_prompt();
        }
    }

    return;
}
