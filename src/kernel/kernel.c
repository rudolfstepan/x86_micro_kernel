
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
    parse_multiboot_info(multiboot_info);
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
    
    //printf("Syscall table address: %p\n", syscall_table);
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
