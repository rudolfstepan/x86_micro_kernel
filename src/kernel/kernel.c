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
#include "drivers/fdd/fdd.h"

#include "multibootheader.h"

// for the keyboard
extern char input_buffer[BUFFER_SIZE];
extern volatile int buffer_index;
extern volatile bool enter_pressed;


// write some diagnostic information to the screen
// void parse_multiboot_info(uint32_t magic, uint32_t* multiboot_info_ptr) {
//     uint32_t total_size = *(uint32_t *)multiboot_info_ptr;
//     multiboot_tag_t *tag = (multiboot_tag_t *)(multiboot_info_ptr + 2); // Skip total size and reserved fields

//     while ((uint8_t *)tag < (uint8_t *)multiboot_info_ptr + total_size) {
//         if (tag->type == 8) { // MULTIBOOT_TAG_TYPE_FRAMEBUFFER
//             multiboot_tag_framebuffer_t *fb_tag = (multiboot_tag_framebuffer_t *)tag;

//             printf("Framebuffer address: 0x%X, ", fb_tag->framebuffer_addr);
//             printf("pitch: %u, ", fb_tag->framebuffer_pitch);
//             printf("width: %u, ", fb_tag->framebuffer_width);
//             printf("height: %u, ", fb_tag->framebuffer_height);
//             printf("bpp: %u\n", fb_tag->framebuffer_bpp);

//             if (fb_tag->framebuffer_addr == 0) {
//                 printf("Invalid framebuffer address.\n");
//                 return;
//             }

//             // // Draw a red rectangle on the screen for 16 bpp (RGB565 format)
//             // if (fb_tag->framebuffer_bpp == 16) {
//             //     uint16_t color = (31 << 11) | (0 << 5) | 0; // Red color in RGB565 (R=31, G=0, B=0)

//             //     for (int y = 0; y < fb_tag->framebuffer_height / 2; y++) {
//             //         for (int x = 0; x < fb_tag->framebuffer_width / 2; x++) {
//             //             uint16_t *pixel = (uint16_t *)(fb_tag->framebuffer_addr + (y * fb_tag->framebuffer_pitch) + (x * 2));
//             //             *pixel = color; // Write the color value to the pixel
//             //         }
//             //     }
//             // } else {
//             //     printf("Unsupported bits per pixel: %u\n", fb_tag->framebuffer_bpp);
//             // }

//             return; // Exit after processing the framebuffer tag
//         } else if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
//             multiboot_tag_mmap_t *mmap_tag = (multiboot_tag_mmap_t *)tag;
//             printf("Memory Map:\n");
//             for (uint8_t *entry_ptr = (uint8_t *)mmap_tag->entries;
//                  entry_ptr < (uint8_t *)mmap_tag + mmap_tag->size;
//                  entry_ptr += mmap_tag->entry_size) {

//                 struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)entry_ptr;
//                 printf("  Address: 0x%p, Length: 0x%u, Type: %u\n",
//                        entry->addr, entry->len, entry->type);
//             }
//         }

//         // Move to the next tag; align to 8 bytes
//         tag = (multiboot_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7));
//     }

//     printf("Framebuffer tag not found or invalid.\n");
// }


void parse_multiboot_info(uint32_t magic, uint32_t* multiboot_info_ptr) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        printf("Invalid magic number: 0x%X\n", magic);
        return;
    }

    uint32_t total_size = *(uint32_t *)multiboot_info_ptr;
    multiboot_tag_t *tag = (multiboot_tag_t *)(multiboot_info_ptr + 2); // Skip total size and reserved fields

    while ((uint8_t *)tag < (uint8_t *)multiboot_info_ptr + total_size) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            multiboot_tag_mmap_t *mmap_tag = (multiboot_tag_mmap_t *)tag;
            printf("Memory Map:\n");

            multiboot_mmap_entry_t *entry = mmap_tag->entries;
            while ((uint8_t *)entry < (uint8_t *)mmap_tag + mmap_tag->size) {
                printf("  Address: 0x%llX, Length: 0x%llX, Type: %u\n", entry->addr, entry->len, entry->type);

                // Move to the next entry
                entry = (multiboot_mmap_entry_t *)((uint8_t *)entry + mmap_tag->entry_size);
            }
        }

        // Move to the next tag; align to 8 bytes
        tag = (multiboot_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    printf("Memory map parsing complete.\n");
}

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

    // check if the current drive is mounted
    if (current_drive == NULL) {
        printf(">");
        set_color(WHITE);
        return;
    }

    printf("%s%s>", current_drive->name, current_path);
    set_color(WHITE);
}

void set_graphics_mode() {
    asm volatile (
        "mov $0x00, %%ah \n"
        "mov $0x13, %%al \n"
        "int $0x10      \n"
        :
        :
        : "ah", "al"
    );
}

//---------------------------------------------------------------------------------------------
// kernel_main is the main entry point of the kernel
// It is called by the bootloader after setting up the environment
// and passing the multiboot information structure
// Parameters:
// multiboot_magic: the magic number passed by the bootloader
// multiboot_info_ptr: a pointer to the multiboot information structure
//---------------------------------------------------------------------------------------------
void kernel_main(uint32_t multiboot_magic, uint32_t* multiboot_info_ptr) {
    //sys_mb_info = multiboot_info_ptr;

    // //Check if the magic number is correct
    // if (multiboot_magic != MULTIBOOT2_HEADER_MAGIC) {
    //     //set_color(RED);
    //     printf("Invalid magic number: 0x%p\n", multiboot_magic);
    //     set_color(WHITE);
    //     return;
    // }

    initialize_memory_system();
    gdt_install();
    idt_install();
    isr_install();
    irq_install();
    initialize_syscall_table();

    // Install the IRQ handler for the timer
    irq_install_handler(0, timer_irq_handler);
    // Install the IRQ handler for the keyboard
    irq_install_handler(1, kb_handler);
    // Install the IRQ handler for the FDD
    irq_install_handler(6, fdd_irq_handler);


    __asm__ __volatile__("sti"); // enable interrupts

    parse_multiboot_info(multiboot_magic, multiboot_info_ptr);


    //set_graphics_mode();

    // display_kernel_banner();
    // display_ascii_art();
    display_welcome_message();
    
    test_memory();
    timer_install(); // Install the timer first
    kb_install(); // Install the keyboard
    //init_drives(); // Initialize drives
    //printf("Type HELP for command list.\n");

    // detect fdd drives
    fdd_detect_drives();
    ata_detect_drives();
    
    // printf("Delay Test 5 Seconds\n");
    // sleep_ms(5000);
    // printf("Delay Test 5 Seconds finished\n");

    // printf("Beep Test\n");
    //beep(1000, 5000);

    //display_color_test();
    print_fancy_prompt();

    // sample interrupt
    Registers regs;

    // Setup to print a character using BIOS interrupt 0x10
    // Setup for BIOS interrupt 0x10 to clear the screen (scroll up)
    regs.ax = 0x0600;           // AH = 0x06 (scroll up), AL = 0 (clear entire window)
    regs.bx = 0x0700;           // BH = text attribute (e.g., 0x07 for white on black)
    regs.cx = 0x0000;           // CH = top row, CL = left column (top-left corner)
    regs.dx = 0x184F;           // DH = bottom row (24), DL = right column (79) for 80x25 mode
    regs.si = 0x0000;           // Not used in this call
    regs.di = 0x0000;           // Not used in this call
    regs.es = 0x0000;           // Not used in this call
    regs.ds = 0x0000;           // Not used in this call
    regs.interruptNumber = 0x10; // BIOS video interrupt

    // Call the assembly function
    //trigger_interrupt(&regs);

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
