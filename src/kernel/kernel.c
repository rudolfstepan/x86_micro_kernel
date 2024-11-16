
#include <stdbool.h>
#include "command.h"
#include "prg.h"
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

extern char _kernel_end;  // Defined by the linker script

#define HEAP_START  ((void*)(&_kernel_end))
#define HEAP_END    (void*)0x500000  // End of heap (5MB)
#define ALIGN_UP(addr, align) (((addr) + ((align)-1)) & ~((align)-1))
#define HEAP_START_ALIGNED ALIGN_UP((size_t)HEAP_START, 16)
#define BLOCK_SIZE sizeof(memory_block)

typedef struct memory_block {
    size_t size;
    int free;
    struct memory_block *next;
} memory_block;

memory_block *freeList = (memory_block*)HEAP_START;

void initialize_memory_system() {
    freeList = (memory_block*)HEAP_START_ALIGNED;
    freeList->size = (size_t)HEAP_END - (size_t)HEAP_START_ALIGNED - BLOCK_SIZE;
    freeList->free = 1;
    freeList->next = NULL;

    printf("Aligned HEAP_START: 0x%p\n", freeList);
}

void k_free(void* ptr) {
    if (!ptr) return;

    memory_block* block = (memory_block*)((char*)ptr - BLOCK_SIZE);
    block->free = 1;

    // Merge with next block if it's free
    if (block->next && block->next->free) {
        block->size += block->next->size + BLOCK_SIZE;
        block->next = block->next->next;
    }

    // Merge with previous block if it's free
    memory_block* current = freeList;
    while (current) {
        if (current->next == block && current->free) {
            current->size += block->size + BLOCK_SIZE;
            current->next = block->next;
            break;
        }
        current = current->next;
    }
}

void* k_malloc(size_t size) {
    memory_block* current = freeList;

    while (current) {
        if (current->free && current->size >= size) {
            current->free = 0; // Mark as allocated

            // Optionally split the block if it's larger than needed
            if (current->size > size + BLOCK_SIZE) {
                memory_block* newBlock = (memory_block*)((char*)current + BLOCK_SIZE + size);
                newBlock->size = current->size - size - BLOCK_SIZE;
                newBlock->free = 1;
                newBlock->next = current->next;

                current->size = size;
                current->next = newBlock;
            }

            return (void*)((char*)current + BLOCK_SIZE); // Return usable memory
        }
        current = current->next;
    }

    // Out of memory
    return NULL;
}

void* k_realloc(void *ptr, size_t new_size) {
    // If `ptr` is NULL, behave like `malloc`
    if (ptr == NULL) {
        return k_malloc(new_size);
    }

    // If `new_size` is 0, behave like `free` and return NULL
    if (new_size == 0) {
        k_free(ptr);
        return NULL;
    }

    // Get the memory block header
    memory_block *block = (memory_block*)((char*)ptr - BLOCK_SIZE);
    size_t old_size = block->size;

    // If the new size is the same or smaller, reuse the existing block
    if (new_size <= old_size) {
        return ptr; // No need to reallocate
    }

    // Allocate a new memory block large enough for the new size
    void *new_ptr = k_malloc(new_size);
    if (!new_ptr) {
        // If allocation fails, return NULL without affecting the old block
        return NULL;
    }

    // Copy data from the old block to the new block
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memmove(new_ptr, ptr, copy_size); // Use `memmove` to handle overlapping regions

    // Free the old memory block
    k_free(ptr);

    // Return the pointer to the new memory block
    return new_ptr;
}


//---------------------------------------------------------------------------------------------

// test methods
void test_realloc() {
    void* ptr = k_malloc(10);
    printf("Allocated: %p\n", ptr);

    ptr = k_realloc(ptr, 20);
    printf("Reallocated (larger): %p\n", ptr);

    ptr = k_realloc(ptr, 5);
    printf("Reallocated (smaller): %p\n", ptr);

    k_free(ptr);
    printf("Memory freed.\n");
}
void TestResetAfterFree() {
    void* firstPtr = k_malloc(1);
    printf("First allocation: %p\n", firstPtr);
    k_free(firstPtr); // This should store the initial alignment padding
    void* secondPtr = k_malloc(1);
    printf("Second allocation: %p\n", secondPtr);
    if (firstPtr == secondPtr) {
        printf("TestResetAfterFree: Passed\n");
    } else {
        printf("TestResetAfterFree: Failed. Expected: %p, Got: %p\n", firstPtr, secondPtr);
    }
}
void TestMultipleFrees() {
    k_free(NULL);
    k_free(NULL);
    void* ptr = k_malloc(1);

    if (ptr != NULL) {
        //printf("TestMultipleFrees: Passed\n");
    } else {
        printf("TestMultipleFrees: Failed\n");
    }
}
void TestSetMemory() {
    char* buffer = (char*)k_malloc(10);
    memset(buffer, 'A', 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 'A') {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestSetMemory: Passed\n");
    } else {
        printf("TestSetMemory: Failed\n");
    }
}
void TestSetZero() {
    char* buffer = (char*)k_malloc(10);
    memset(buffer, 0, 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 0) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestSetZero: Passed\n");
    } else {
        printf("TestSetZero: Failed\n");
    }
}
void TestNullPointerMemset() {
    if (memset(NULL, 0, 10) == NULL) {
        //printf("TestNullPointerMemset: Passed\n");
    } else {
        printf("TestNullPointerMemset: Failed\n");
    }
}
void TestCopyNonOverlapping() {
    char src[10] = "123456789";
    char dest[10];
    memcpy(dest, src, 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (dest[i] != src[i]) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestCopyNonOverlapping: Passed\n");
    } else {
        printf("TestCopyNonOverlapping: Failed\n");
    }
}
void TestCopyOverlapping() {
    char buffer[20] = "123456789";
    memcpy(buffer + 4, buffer, 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (buffer[i + 4] != buffer[i]) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestCopyOverlapping: Passed\n");
    } else {
        printf("TestCopyOverlapping: Failed\n");
    }
}
void TestNullPointerSrc() {
    char dest[10];
    if (memcpy(dest, NULL, 10) == NULL) {
        //printf("TestNullPointerSrc: Passed\n");
    } else {
        printf("TestNullPointerSrc: Failed\n");
    }
}
void TestNullPointerDest() {
    char src[10] = "123456789";
    if (memcpy(NULL, src, 10) == NULL) {
        //printf("TestNullPointerDest: Passed\n");
    } else {
        printf("TestNullPointerDest: Failed\n");
    }
}

int test_memory() {
    printf("Testing Memory...\n");
    test_realloc();
    TestResetAfterFree();
    TestMultipleFrees();
    TestSetMemory();
    TestSetZero();
    TestNullPointerMemset();
    TestCopyNonOverlapping();
    TestCopyOverlapping();
    TestNullPointerSrc();
    TestNullPointerDest();
    printf("done\n");
    return 0;
}


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
    
    // Install the IRQ handler for the timer
    irq_install_handler(0, timer_irq_handler);
    // Install the IRQ handler for the keyboard
    irq_install_handler(1, kb_handler);
    // Install the IRQ handler for the FDD
    irq_install_handler(6, fdd_irq_handler);

    __asm__ __volatile__("sti"); // enable interrupts

    parse_multiboot_info(multiboot_magic, multiboot_info_ptr);

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
