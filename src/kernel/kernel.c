
#include <stdbool.h>
#include "command.h"
#include "prg.h"
#include "sys.h"
#include "pit.h"

#include "drivers/kb/kb.h"
#include "drivers/rtc/rtc.h"
#include "drivers/video/video.h"
#include "drivers/io/io.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

#include "filesystem/filesystem.h"
#include "drivers/ata/ata.h"
#include "drivers/fdd/fdd.h"

#include "mbheader.h"
#include "memory.h"


//---------------------------------------------------------------------------------------------

// for the keyboard
extern char input_buffer[128];
extern volatile int buffer_index;
extern volatile bool enter_pressed;

//---------------------------------------------------------------------------------------------
// syscall table entry points and definitions
//---------------------------------------------------------------------------------------------
void kernel_hello() {
    printf("Hello from the kernel. All engines running.\n");
}

void kernel_print_number(int number) {
    printf("Kernel received number: %d\n", number);
}

void* syscall_table[512] __attribute__((section(".syscall_table"))) = {
    (void*)&vga_write_char,      // Syscall 0: One arguments
    (void*)&kernel_print_number, // Syscall 1: One argument
    (void*)&pit_delay,          // Syscall 2: One argument
    (void*)&kb_wait_enter,      // Syscall 3: No arguments
    (void*)&k_malloc,           // Syscall 4: One argument
    (void*)&k_free,             // Syscall 5: One argument
    (void*)&k_realloc,          // Syscall 6: 2 arguments
    // Add more syscalls here
};

// the syscall handler function called by the interrupt handler in the boot.asm file
void syscall_handler(void* irq_number) {
    int syscall_index, arg1, arg2, arg3;
    // Retrieve values from eax, ebx, ecx, edx, and esi into C variables
    __asm__ __volatile__(""  // No actual instructions needed; just retrieve registers
        : "=a"(syscall_index),  // Load eax into syscall_index
        "=b"(arg1),           // Load ebx into arg1
        "=c"(arg2),           // Load ecx into arg2
        "=d"(arg3)            // Load edx into arg3
        :                       // No input operands
    );
    // Print the values for debugging purposes
    //printf("Syscall index: %d, Arguments: %d, %d, %d\n", syscall_index, arg1, arg2, arg3);
    // Ensure the index is within bounds
    if (syscall_index < 0 || syscall_index >= 512 || syscall_table[syscall_index] == 0) {
        printf("Invalid syscall index: %d\n", syscall_index);
        return;
    }
    // Retrieve the function pointer from the table
    void* func_ptr = (void*)syscall_table[syscall_index];
    // Call functions based on syscall index, handling arguments conditionally
    switch (syscall_index) {
        case 0:  // kernel_hello - No arguments
        case 3:  // kb_wait_enter - No arguments
        case SYS_FREE:  // k_free - No argument
            ((void (*)(void))func_ptr)();
            break;

        case 1:  // kernel_print_number - One argument
        case 2:  // delay - One argument
        case SYS_MALLOC:  // k_malloc - One argument
            ((void (*)(int))func_ptr)((uint32_t)arg1);
            break;

        // Add additional cases for syscalls with more arguments
        case SYS_REALLOC:  // k_realloc - Two arguments
            ((void* (*)(void*, size_t))func_ptr)((void*)arg1, (size_t)arg2);
            break;

        default:
            printf("Unknown syscall index: %d\n", syscall_index);
            break;
    }
}

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


// void parse_multiboot_info(uint32_t magic, uint32_t* multiboot_info_ptr) {
//     if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
//         printf("Invalid magic number: 0x%X\n", magic);
//         return;
//     }

//     uint32_t total_size = *(uint32_t *)multiboot_info_ptr;
//     multiboot_tag_t *tag = (multiboot_tag_t *)(multiboot_info_ptr + 2); // Skip total size and reserved fields

//     while ((uint8_t *)tag < (uint8_t *)multiboot_info_ptr + total_size) {
//         if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
//             multiboot_tag_mmap_t *mmap_tag = (multiboot_tag_mmap_t *)tag;
//             printf("Memory Map:\n");

//             multiboot_mmap_entry_t *entry = mmap_tag->entries;
//             while ((uint8_t *)entry < (uint8_t *)mmap_tag + mmap_tag->size) {
//                 printf("  Address: 0x%llX, Length: 0x%llX, Type: %u\n", entry->addr, entry->len, entry->type);

//                 // Move to the next entry
//                 entry = (multiboot_mmap_entry_t *)((uint8_t *)entry + mmap_tag->entry_size);
//             }
//         }

//         // Move to the next tag; align to 8 bytes
//         tag = (multiboot_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7));
//     }

//     printf("Memory map parsing complete.\n");
// }

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
    // set_color(LIGHT_MAGENTA);
    // printf("\n");
    // printf("      *------------------------------------------------------------*\n");
    // printf("      |        Welcome to the Rudolf Stepan x86 Micro Kernel       |\n");
    // printf("      |      Type 'HELP' for a list of commands and instructions   |\n");
    // printf("      *------------------------------------------------------------*\n");
    // printf("\n");
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

void parse_multiboot_info(multiboot_info_t *mb_info) {
    if (mb_info->flags & MULTIBOOT_FLAG_MEM) {
        // Memory info available
        uint32_t mem_lower_kb = mb_info->mem_lower;
        uint32_t mem_upper_kb = mb_info->mem_upper;
        printf("Memory: Lower = %u KB, Upper = %u KB\n", mem_lower_kb, mem_upper_kb);
    }

    if (mb_info->flags & MULTIBOOT_FLAG_BOOT_DEVICE) {
        // Boot device info available
        printf("Boot device: 0x%x\n", mb_info->boot_device);
    }

    if (mb_info->flags & MULTIBOOT_FLAG_CMDLINE) {
        // Kernel command line available
        printf("Command line: %s\n", mb_info->cmdline);
    }

    if (mb_info->flags & MULTIBOOT_FLAG_MODS) {
        // Modules info available
        printf("Modules count: %u\n", mb_info->mods_count);
        for (uint32_t i = 0; i < mb_info->mods_count; i++) {
            multiboot_module_t *mod = &mb_info->mods_addr[i];
            printf("Module %u: Start = 0x%x, End = 0x%x, String = %s\n",
                   i, mod->mod_start, mod->mod_end, mod->string);
        }
    }

    if (mb_info->flags & (1 << 6)) { // Bit 6 indicates memory map availability
        printf("Memory map available:\n");
        multiboot_mmap_entry_t *mmap = mb_info->mmap_addr;
        for (uint32_t i = 0; i < mb_info->mmap_length / sizeof(multiboot_mmap_entry_t); i++) {
            printf("Base Addr = 0x%x%x, Length = 0x%x%x, Type = %u\n",
                   mmap[i].base_addr_high, mmap[i].base_addr_low,
                   mmap[i].length_high, mmap[i].length_low,
                   mmap[i].type);
        }
    }
}

//---------------------------------------------------------------------------------------------
// kernel_main is the main entry point of the kernel
// It is called by the bootloader after setting up the environment
// and passing the multiboot information structure
// Parameters:
// multiboot_magic: the magic number passed by the bootloader
// multiboot_info_ptr: a pointer to the multiboot information structure
//---------------------------------------------------------------------------------------------
void kernel_main(uint32_t multiboot_magic, uint32_t* multiboot_info) {
    //sys_mb_info = multiboot_info_ptr;

    printf("Parsing multiboot info...\n");

    parse_multiboot_info(multiboot_info);

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
    
    // Install the IRQ handler for the timer
    irq_install_handler(0, timer_irq_handler);
    // Install the IRQ handler for the keyboard
    irq_install_handler(1, kb_handler);
    // Install the IRQ handler for the FDD
    irq_install_handler(6, fdd_irq_handler);

    __asm__ __volatile__("sti"); // enable interrupts

    display_welcome_message();
    
    test_memory();
    timer_install(1); // Install the timer first for 1 ms delay
    kb_install(); // Install the keyboard

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
    
    printf("Syscall table address: %p\n", syscall_table);
    //display_color_test();
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
