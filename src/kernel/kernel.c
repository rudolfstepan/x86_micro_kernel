#include "kernel.h"

#include <stdbool.h>
#include "command.h"
#include "prg.h"
#include "sys.h"
#include "pit.h"
#include "hpet.h"
#include "apic.h"
#include "scheduler.h"
#include "process.h"
#include "drivers/pci.h"

#include "drivers/kb/kb.h"
#include "drivers/rtc/rtc.h"
// #include "drivers/video/vga.h"
#include "drivers/io/io.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"

#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "drivers/ata/ata.h"
#include "drivers/fdd/fdd.h"

#include "drivers/video/framebuffer.h"
#include "multiboot.h"

//#include "mbheader.h"
#include "memory.h"
//#include "paging.h"
// #include "drivers/network/rtl8139.h"
// #include "drivers/network/e1000.h"
#include "drivers/network/ne2000.h"
// #include "drivers/network/vmxnet3.h"

extern char _stack_start;  // Start address of the stack
extern char _stack_end;    // End address of the stack
extern volatile size_t total_memory;
volatile uint64_t cpu_frequency = 0; // Global CPU frequency

// Helper: Load CR3 (Page Directory Base Register)
static inline void load_cr3(uint32_t address) {
    asm volatile("mov %0, %%cr3" : : "r"(address));
}

// Helper: Read CR0
static inline uint32_t read_cr0() {
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

// Helper: Write CR0
static inline void write_cr0(uint32_t cr0) {
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

// Helper: Flush TLB
static inline void flush_tlb() {
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");
}

static inline uint64_t read_cpu_cycle_counter() {
    uint32_t high, low;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

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
    //(void*)&vga_write_char,     // Syscall 0: One arguments
    (void*)&fb_write_char,       // Syscall 0: No arguments
    (void*)&kernel_print_number,// Syscall 1: One argument
    (void*)&pit_delay,          // Syscall 2: One argument
    (void*)&kb_wait_enter,      // Syscall 3: No arguments
    (void*)&k_malloc,           // Syscall 4: One argument
    (void*)&k_free,             // Syscall 5: One argument
    (void*)&k_realloc,          // Syscall 6: 2 arguments
    (void*)&getchar,            // Syscall 7: No arguments
    (void*)&register_interrupt_handler,// Syscall 8: 2 arguments
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

        case SYS_TERMINAL_GETCHAR:  // kb_getchar - No arguments
            ((void* (*)(void))func_ptr)();
            break;

        case SYS_INSTALL_IRQ:  // kb_install - 2 arguments
            ((void (*)(int, void*))func_ptr)(arg1, (void*)arg2);
            break;

        default:
            printf("Unknown syscall index: %d\n", syscall_index);
            break;
    }
}

void print_welcome_message() {
    set_color(WHITE);
    printf("\n");
    printf("      *------------------------------------------------------------*\n");
    printf("      |        Welcome to the Rudolf Stepan x86 Micro Kernel       |\n");
    printf("      |      Type 'HELP' for a list of commands and instructions   |\n");
    printf("      *------------------------------------------------------------*\n");
    printf("        Total Memory: %d MB\n", total_memory/1024/1024);
    printf("        Detected Drives (%d): ", drive_count);
    for(int i=0; i<drive_count; i++){
        const char* drive_type = (const char*)(detected_drives[i].type == DRIVE_TYPE_ATA ? "ATA" : "FDD");
        printf(" %s: %s ", drive_type, detected_drives[i].name);
    }

    //printf("\nUser memory address: %p\n", USER_MEMORY_ADDRESS);

    printf("\n\n    Enter a Command or help for a complete list of supported commands.\n");
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
    //str_trim_end(current_path, '/');

    // check if the current drive is mounted
    if (current_drive == NULL) {
        printf(">");
        set_color(WHITE);
        return;
    }

    //printf("%s%s>", current_drive->name, current_path);
    printf(">");

    set_color(WHITE);
}

void task1() {
    int counter = 0;
    printf("+++Task 1 started\n");
    while (1) {
       counter++;
       //delay_ms(1000);
       //if(counter == 1000){
           //printf("Task 1 running...\n");
           counter = 0;
           asm volatile("int $0x29"); // Trigger a timer interrupt
       //}
    }
}

void task2() {
    int counter = 0;
    printf("+++Task 2 started\n");
    while (1) {
       counter++;
       //delay_ms(1000);
       //if(counter == 1000){
           //printf("Task 2 running...\n");
           counter = 0;
           asm volatile("int $0x29"); // Trigger a timer interrupt
       //}
    }
}

extern fat32_class_t fat32;
//---------------------------------------------------------------------------------------------
// kernel_main is the main entry point of the kernel
// It is called by the bootloader after setting up the environment
// and passing the multiboot information structure
// Parameters:
// multiboot_magic: the magic number passed by the bootloader
// multiboot_info_ptr: a pointer to the multiboot information structure
//---------------------------------------------------------------------------------------------
void kernel_main(uint32_t *multiboot_magic, multiboot2_info_t *multiboot_info) {

    enumerate_multiboot2_tags(multiboot_info);

    initialize_memory_system();

    //init_paging();
    //test_paging();

    gdt_install();
    idt_install();
    isr_install();
    irq_install();
    timer_install(1); // Install the timer first for 1 ms delay
    kb_install(); // Install the keyboard
    fdc_initialize();

    test_memory();

    // Initialize the HPET timer
    //hpet_init(); // hpet not working
    initialize_apic_timer();

    // Initialize PCI subsystem
    pci_init();

    register_interrupt_handler(9, scheduler_interrupt_handler);

    //rtl8139_detect();
    //e1000_detect();
    //ne2000_detect();
    //vmxnet3_setup();
    
    pci_probe_drivers();

    __asm__ __volatile__("sti"); // enable interrupts

    //display_welcome_message();
    // printf("Press any key to continue...\n");
    // getchar();
    // printf("Press any key to continue...\n");
    // getchar();

    //wait_enter_pressed();

    //calc the cpu speed
    volatile uint64_t start_cycles, end_cycles;
    start_cycles = read_cpu_cycle_counter();
    pit_delay(1000); // Hardware timer-based delay
    end_cycles = read_cpu_cycle_counter();
    cpu_frequency = end_cycles - start_cycles; // Cycles per second (Hz)

    ata_detect_drives();
    current_drive = ata_get_drive(0);
    if (current_drive) {
        // Initialize the FAT32 file system
        //ctor_fat32_class(&fat32);

        init_fs(current_drive);
    } else {
        printf("Drive not found.\n");
    }
    // detect fdd drives
    fdd_detect_drives();

    create_process(task1, "Task 1");
    create_process(command_loop, "Command Loop");

    // Initialize the APIC timer
    init_apic_timer(1000000);  // Set timer ticks

    //command_loop();

    while (1) {
        //printf("Kernel Main Loop\n");
        delay_ms(100);
        asm volatile("int $0x29"); // Trigger a timer interrupt
    }   

}
