#include <stdbool.h>
#include "command.h"
#include "prg.h"
#include "sys.h"
#include "pit.h"
#include "hpet.h"
#include "apic.h"
#include "scheduler.h"
#include "process.h"

#include "drivers/kb/kb.h"
#include "drivers/rtc/rtc.h"
#include "drivers/video/video.h"
#include "drivers/io/io.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

#include "filesystem/filesystem.h"
#include "filesystem/fat32/fat32.h"
#include "drivers/ata/ata.h"
#include "drivers/fdd/fdd.h"

#include "mbheader.h"
#include "memory.h"
//#include "paging.h"


extern char _stack_start;  // Start address of the stack
extern char _stack_end;    // End address of the stack

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
    (void*)&vga_write_char,     // Syscall 0: One arguments
    (void*)&kernel_print_number,// Syscall 1: One argument
    (void*)&pit_delay,          // Syscall 2: One argument
    (void*)&kb_wait_enter,      // Syscall 3: No arguments
    (void*)&k_malloc,           // Syscall 4: One argument
    (void*)&k_free,             // Syscall 5: One argument
    (void*)&k_realloc,          // Syscall 6: 2 arguments
    (void*)&getchar,            // Syscall 7: No arguments
    (void*)&irq_install_handler,// Syscall 8: 2 arguments
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

// Function to Print EFI Memory Map
void print_efi_memory_map(const multiboot2_info_t *mb_info) {
    const multiboot2_tag_t *tag = mb_info->tags;

    // Iterate through all Multiboot2 tags
    while (tag->type != MULTIBOOT2_INFO_TAG_END) {
        if (tag->type == MULTIBOOT2_INFO_TAG_EFI_MMAP) {
    const multiboot2_tag_efi_mmap_t *efi_mmap_tag = (const multiboot2_tag_efi_mmap_t *)tag;
    const efi_memory_descriptor_t *desc = (const efi_memory_descriptor_t *)efi_mmap_tag->efi_memory_map;
    uint8_t *end = (uint8_t *)efi_mmap_tag + efi_mmap_tag->size;

    printf("EFI Memory Map:\n");
    printf("-------------------------------------------------------------\n");
    printf("| Type | Physical Start | Number of Pages | Attributes      |\n");
    printf("-------------------------------------------------------------\n");

    // Iterate over the descriptors
    while ((uint8_t *)desc + efi_mmap_tag->descriptor_size <= end) {
        // Print the current descriptor
        printf("| %4u | 0x%013llx | %15llu | 0x%016llx |\n",
               desc->type,
               desc->physical_start,
               desc->num_pages,
               desc->attribute);

        // Advance to the next descriptor
        desc = (const efi_memory_descriptor_t *)((uint8_t *)desc + efi_mmap_tag->descriptor_size);
    }

    printf("-------------------------------------------------------------\n");

    // Debugging: Print the total tag size and descriptor size
    printf("Debug: EFI MMap tag size: %u, Descriptor size: %u\n",
           efi_mmap_tag->size, efi_mmap_tag->descriptor_size);
}
        // Move to the next tag
        tag = (const multiboot2_tag_t *)((uint8_t *)tag + tag->size);
    }
}

void parse_multiboot2_info(const multiboot2_info_t *mb_info) {
    multiboot2_tag_t *tag = (multiboot2_tag_t *)(mb_info->tags);

    while (tag->type != MULTIBOOT2_INFO_TAG_END) {
        switch (tag->type) {
            case MULTIBOOT2_INFO_TAG_CMDLINE:
                printf("Command Line: %s\n", ((multiboot2_tag_cmdline_t *)tag)->cmdline);
                break;
            case MULTIBOOT2_INFO_TAG_BOOT_LOADER_NAME:
                printf("Bootloader Name: %s\n", ((multiboot2_tag_boot_loader_name_t *)tag)->name);
                break;
            case MULTIBOOT2_INFO_TAG_BASIC_MEMINFO:
                printf("Basic Memory Info: Lower = %u KB, Upper = %u KB\n",
                       ((multiboot2_tag_basic_meminfo_t *)tag)->mem_lower,
                       ((multiboot2_tag_basic_meminfo_t *)tag)->mem_upper);
                break;
            case MULTIBOOT2_INFO_TAG_MMAP:
                printf("Memory Map available\n");
                break;
            case MULTIBOOT2_INFO_TAG_MODULE:
                printf("Module available\n");
                break;
            case MULTIBOOT2_INFO_TAG_EFI_MMAP:
                print_efi_memory_map(mb_info);
                break;
            default:
                printf("Unknown tag type: %u\n", tag->type);
                break;
        }
        tag = (multiboot2_tag_t *)((uint8_t *)tag + tag->size);
    }
}

// Function to compute the total usable memory from the Multiboot2 memory map
uint64_t compute_total_memory(const multiboot2_info_t *mb_info) {
    uint64_t total_memory = 0;

    // Start parsing the tags
    const multiboot2_tag_t *tag = (const multiboot2_tag_t *)(mb_info->tags);
    while (tag->type != MULTIBOOT2_INFO_TAG_END) {
        if (tag->type == MULTIBOOT2_INFO_TAG_MMAP) {
            const multiboot2_tag_mmap_t *mmap_tag = (const multiboot2_tag_mmap_t *)tag;
            const multiboot2_mmap_entry_t *mmap_entry = mmap_tag->entries;

            printf("Memory map available:\n");
            printf("Entry size: %u\n", mmap_tag->entry_size);

            // Iterate over all memory map entries
            while ((uint8_t *)mmap_entry < (uint8_t *)mmap_tag + mmap_tag->size) {
                if (mmap_entry->type == 1) { // Usable memory
                    total_memory += mmap_entry->length;
                }
                mmap_entry = (const multiboot2_mmap_entry_t *)((uint8_t *)mmap_entry + mmap_tag->entry_size);
            }
        }

        // Move to the next tag
        tag = (const multiboot2_tag_t *)((uint8_t *)tag + tag->size);
    }

    return total_memory;
}

void parse_multiboot1_info(const multiboot1_info_t *mb_info) {
    printf("Parsing Multiboot1 Information...\n");

    // Check flags for available fields
    if (mb_info->flags & MULTIBOOT1_FLAG_MEM) {
        printf("Memory Information: ");
        printf("  Lower Memory: %u KB, ", mb_info->mem_lower);
        printf("  Upper Memory: %u KB\n", mb_info->mem_upper);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_BOOT_DEVICE) {
        printf("Boot Device: %p\n", mb_info->boot_device);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_CMDLINE) {
        const char *cmdline = (const char *)mb_info->cmdline;
        printf("Command Line: %s\n", cmdline);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_MODS) {
        printf("Modules:\n");
        const multiboot1_module_t *mods = (const multiboot1_module_t *)mb_info->mods_addr;
        for (uint32_t i = 0; i < mb_info->mods_count; i++) {
            printf("  Module %u:\n", i + 1);
            printf("    Start Address: 0x%x\n", mods[i].mod_start);
            printf("    End Address: 0x%x\n", mods[i].mod_end);
            const char *mod_cmdline = (const char *)mods[i].string;
            printf("    Command Line: %s\n", mod_cmdline ? mod_cmdline : "(none)");
        }
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_MMAP) {
        printf("Memory Map:\n");
        const multiboot1_mmap_entry_t *mmap = (const multiboot1_mmap_entry_t *)mb_info->mmap_addr;
        const uint8_t *mmap_end = (const uint8_t *)mb_info->mmap_addr + mb_info->mmap_length;

        printf("------------------------------------------------------------\n");
        printf("| Address                 | Length        | Type (1=Usable)|\n");
        printf("------------------------------------------------------------\n");

        while ((uint8_t *)mmap < mmap_end) {
            //printf("| %p | %u | %u |\n", mmap->base_addr, mmap->length, mmap->type);

            printf("| %-12p ", mmap->base_addr);
            printf("| %-12p | ", mmap->base_addr + mmap->length -1);
            printf("%-13u | ", mmap->length);
            printf("%-14u |\n", mmap->type);

            // calculate total memory
            total_memory += mmap->length;

            // Advance to the next entry
            mmap = (const multiboot1_mmap_entry_t *)((uint8_t *)mmap + mmap->size + sizeof(mmap->size));
        }
        printf("------------------------------------------------------------\n");
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_BOOTLOADER) {
        const char *bootloader_name = (const char *)mb_info->boot_loader_name;
        printf("Bootloader Name: %s\n", bootloader_name);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_VBE) {
        printf("VBE Information:\n");
        printf("Control Info: %p ", mb_info->vbe_control_info);
        printf("Mode Info: %p ", mb_info->vbe_mode_info);
        printf("Mode: %p\n", mb_info->vbe_mode);
        printf("Interface Segment: %p ", mb_info->vbe_interface_seg);
        printf("Offset: %p ", mb_info->vbe_interface_off);
        printf("Length: %u\n", mb_info->vbe_interface_len);
    }

    if (mb_info->flags & MULTIBOOT1_FLAG_APM) {
        printf("APM Table Address: 0x%x\n", mb_info->apm_table);
    }

    printf("Parsing Complete.\n");
}


void task1() {
    int counter = 0;
    printf("+++Task 1 started\n");
    while (1) {
       counter++;
       //sleep_ms(1000);
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
       //sleep_ms(1000);
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
void kernel_main(uint32_t multiboot_magic, const multiboot1_info_t *multiboot_info){

    // if (!(read_cr0() & CR0_PE)) {
    //     printf("Error: Protected mode is not enabled.\n");
    //     while (1);
    // }

    // Validate Multiboot2 magic number
    if (multiboot_magic != 0x36d76289) {
        printf("Error: Invalid Multiboot2 magic number: 0x%x\n", multiboot_magic);
        while (1) { asm volatile("hlt"); }
    }

    // Check if multiboot_info is valid
    if (multiboot_info == NULL) {
        printf("Error: Multiboot information structure is NULL.\n");
        while (1) { asm volatile("hlt"); }
    }

    parse_multiboot1_info(multiboot_info);

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

    irq_install_handler(9, scheduler_interrupt_handler);

    __asm__ __volatile__("sti"); // enable interrupts

    //display_welcome_message();
    // printf("Press any key to continue...\n");
    // getchar();
    // printf("Press any key to continue...\n");
    // getchar();

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
        ctor_fat32_class(&fat32);

        init_fs(current_drive);
    } else {
        printf("Drive not found.\n");
    }
    // detect fdd drives
    fdd_detect_drives();

    // // printf("Press any key to continue...\n");
    // // getchar();
    // //clear_screen();
    // print_welcome_message();


    // load_program_into_memory("DIR.PRG", 0x01100000);
    // program_header_t* header = (program_header_t*)0x01100000;
    // create_task(0x01100000 + header->entry_point, stack1, sizeof(stack1));

    // load_program_into_memory("TEST.PRG", 0x01200000);
    // header = (program_header_t*)0x01200000;
    // create_task(0x01200000 + header->entry_point, stack2, sizeof(stack2));

    //uintptr_t stack_start = (uintptr_t)&_stack_start;
    // uintptr_t stack_end = (uintptr_t)&_stack_end;

    // printf("Stack Start: 0x%08X, ", stack_start);
    // printf("End:   0x%08X, ", stack_end);
    // printf("Size:  %d bytes\n", stack_end - stack_start);

    // void* stack1 = k_malloc(STACK_SIZE);
    // void* stack2 = k_malloc(STACK_SIZE);
    // // void* stack3 = k_malloc(STACK_SIZE);

    // create_task(task1, stack2);
    // // create_task(task2, stack3);
    // create_task(command_loop, stack1);

    create_process(task1);
    create_process(command_loop);

    // Initialize the APIC timer
    //init_apic_timer(1000000);  // Set timer ticks

    while (1) {
        //printf("Kernel Main Loop\n");
        sleep_ms(100);
        asm volatile("int $0x29"); // Trigger a timer interrupt
    }   

}
