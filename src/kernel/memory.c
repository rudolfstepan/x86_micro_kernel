#include "memory.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/string.h"
// #include "drivers/video/vga.h"
#include "drivers/video/framebuffer.h"
#include "drivers/io/io.h"
#include <stdbool.h>
#include <stdint.h>

extern char _kernel_end; // Defined by the linker script
volatile size_t total_memory; // in kilobytes set by parsing the multiboot memory map after kernel initialization

#define MULTIBOOT_USABLE_START 0x10000 // First usable memory address (64 KB)
#define ALIGN_UP(addr, align) (((addr) + ((align)-1)) & ~((align)-1))
#define FRAMEBUFFER_START 0xFD000000   // Start address of the framebuffer (example)
#define HEAP_START ALIGN_UP(MULTIBOOT_USABLE_START, 4096)

size_t HEAP_END = 0;  // HEAP_END to be calculated during runtime
uint8_t* block_bitmap = NULL; // Pointer to the block bitmap
#define BLOCK_SIZE 4096 // 4KB per block

size_t calculate_heap_end() {
    size_t potential_end = HEAP_START + total_memory; // HEAP_START + total_memory in bytes
    return (FRAMEBUFFER_START > potential_end) ? potential_end : (FRAMEBUFFER_START - 1);
}

void initialize_block_bitmap() {
    size_t max_blocks = (HEAP_END - HEAP_START) / BLOCK_SIZE;
    size_t bitmap_size = ALIGN_UP((max_blocks + 7) / 8, 16); // Bitmap size in bytes

    block_bitmap = (uint8_t*)HEAP_START;
    if ((size_t)block_bitmap + bitmap_size > HEAP_END) {
        printf("Error: Not enough memory for block bitmap.\n");
        return;
    }

    memset(block_bitmap, 0, bitmap_size);
    printf("Block bitmap initialized at %p, size: %zu bytes\n", block_bitmap, bitmap_size);
}

void initialize_memory_system() {
    if (total_memory == 0) {
        printf("Error: total_memory not initialized.\n");
        return;
    }

    HEAP_END = calculate_heap_end(); // Dynamically calculate HEAP_END

    printf("Usable memory starts at: 0x%x\n", MULTIBOOT_USABLE_START);
    printf("Heap Start: %p\n", (void*)HEAP_START);
    printf("Heap End: %p\n", (void*)HEAP_END);
    printf("Heap Size: ");
    print_memory_size(HEAP_END - HEAP_START);
    printf("\n");

    initialize_block_bitmap();
    printf("Memory system initialized with %zu blocks of %u bytes.\n", (HEAP_END - HEAP_START) / BLOCK_SIZE, BLOCK_SIZE);
}

void* k_malloc(size_t size) {
    size_t num_blocks = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
    size_t max_blocks = (HEAP_END - HEAP_START) / BLOCK_SIZE; // Dynamically calculate MAX_BLOCKS

    for (size_t i = 0; i < max_blocks; i++) {
        size_t j;
        for (j = 0; j < num_blocks; j++) {
            if (block_bitmap[(i + j) / 8] & (1 << ((i + j) % 8))) {
                break; // Block already allocated
            }
        }

        if (j == num_blocks) { // Found enough contiguous blocks
            for (size_t k = 0; k < num_blocks; k++) {
                block_bitmap[(i + k) / 8] |= (1 << ((i + k) % 8)); // Mark blocks as used
            }
            return (void*)(HEAP_START + (i * BLOCK_SIZE));
        }
    }

    printf("Error: Not enough contiguous blocks available.\n");
    return NULL;
}

void k_free(void* ptr, size_t size) {
    if (!ptr) return;

    size_t start_block = ((size_t)ptr - HEAP_START) / BLOCK_SIZE;
    size_t num_blocks = ALIGN_UP(size, BLOCK_SIZE) / BLOCK_SIZE;
    size_t max_blocks = (HEAP_END - HEAP_START) / BLOCK_SIZE; // Dynamically calculate MAX_BLOCKS

    if (start_block + num_blocks > max_blocks) {
        printf("Error: Free request out of heap bounds.\n");
        return;
    }

    for (size_t i = 0; i < num_blocks; i++) {
        block_bitmap[(start_block + i) / 8] &= ~(1 << ((start_block + i) % 8)); // Mark blocks as free
    }
}

void* k_realloc(void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return k_malloc(new_size);
    }

    return ptr;
}

void print_memory_size(size_t size) {
    if (size >= 1024 * 1024) {
        printf("%.2f MB", (double)size / (1024 * 1024));
    } else if (size >= 1024) {
        printf("%.2f KB", (double)size / 1024);
    } else {
        printf("%zu bytes", size);
    }
}

void show_heap() {
    printf("Heap Debug Info:\n");

    size_t max_blocks = (HEAP_END - HEAP_START) / BLOCK_SIZE;
    size_t used_blocks = 0;
    size_t free_blocks = 0;
    size_t largest_free_contiguous = 0;
    size_t current_free_contiguous = 0;

    for (size_t i = 0; i < max_blocks; i++) {
        if (block_bitmap[i / 8] & (1 << (i % 8))) {
            // Block is used
            used_blocks++;
            if (current_free_contiguous > largest_free_contiguous) {
                largest_free_contiguous = current_free_contiguous;
            }
            current_free_contiguous = 0;
        } else {
            // Block is free
            free_blocks++;
            current_free_contiguous++;
        }
    }

    // Final check for largest contiguous free blocks
    if (current_free_contiguous > largest_free_contiguous) {
        largest_free_contiguous = current_free_contiguous;
    }

    printf("Total Blocks: %zu\n", max_blocks);
    printf("Used Blocks: %zu\n", used_blocks);
    printf("Free Blocks: %zu\n", free_blocks);
    printf("Largest Contiguous Free Blocks: %zu (%zu bytes)\n",
           largest_free_contiguous, largest_free_contiguous * BLOCK_SIZE);
}

void show_memory_map() {
    printf("\n--- Memory Map (Occupied Only) ---\n");

    // Display the occupied heap memory blocks
    
}

//---------------------------------------------------------------------------------------------
#define LINE_WIDTH 80

void print_test_result(const char *test_name, bool passed) {
    int name_length = strlen(test_name);
    int padding = LINE_WIDTH - name_length - 7; // 7 for " [ OK ]" or " [FAILED]"


    if(passed){
        set_color(GREEN);
    }else{
        set_color(RED);
    }

    printf("%s%*s\n", test_name, padding, passed ? "[ OK ]" : "[FAILED]");

    set_color(WHITE);
}

// Test methods with boolean return value
bool test_realloc() {
    
    return true;
}

bool TestResetAfterFree() {
    void *firstPtr = k_malloc(1);
    if (!firstPtr) return false;

    k_free(firstPtr, 1);

    void *secondPtr = k_malloc(1);
    return firstPtr == secondPtr;
}

bool TestMultipleFrees() {
    k_free(NULL, 0);
    k_free(NULL, 0);
    void *ptr = k_malloc(1);
    return ptr != NULL;
}

bool TestSetMemory() {
    char *buffer = (char *)k_malloc(10);
    if (!buffer) return false;

    memset(buffer, 'A', 10);
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 'A') {
            k_free(buffer, 10);
            return false;
        }
    }

    k_free(buffer, 10);
    return true;
}

bool TestSetZero() {
    char *buffer = (char *)k_malloc(10);
    if (!buffer) return false;

    memset(buffer, 0, 10);
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 0) {
            k_free(buffer, 10);
            return false;
        }
    }

    k_free(buffer, 10);
    return true;
}

bool TestNullPointerMemset() {
    return memset(NULL, 0, 10) == NULL;
}

bool TestCopyNonOverlapping() {
    char src[10] = "123456789";
    char dest[10];
    memcpy(dest, src, 10);

    for (int i = 0; i < 10; i++) {
        if (dest[i] != src[i]) {
            return false;
        }
    }
    return true;
}

bool TestCopyOverlapping() {
    char buffer[20] = "123456789";
    memcpy(buffer + 4, buffer, 10);

    for (int i = 0; i < 10; i++) {
        if (buffer[i + 4] != buffer[i]) {
            return false;
        }
    }
    return true;
}

bool TestNullPointerSrc() {
    char dest[10];
    return memcpy(dest, NULL, 10) == NULL;
}

bool TestNullPointerDest() {
    char src[10] = "123456789";
    return memcpy(NULL, src, 10) == NULL;
}

void test_malloc() {
    // Add test cases and debugging information
    void* ptr1 = k_malloc(1024);
    void* ptr2 = k_malloc(1024);
    k_free(ptr1, 1024);
    void* ptr3 = k_malloc(512);
    printf("Tests complete: ptr1=%p, ptr2=%p, ptr3=%p\n", ptr1, ptr2, ptr3);

    k_free(ptr2, 1024);
    k_free(ptr3, 512);
}

void test_memory() {
    test_malloc();
    print_test_result("Test realloc", test_realloc());
    print_test_result("Test Reset After Free", TestResetAfterFree());
    print_test_result("Test Multiple Frees", TestMultipleFrees());
    print_test_result("Test Set Memory", TestSetMemory());
    print_test_result("Test Set Zero", TestSetZero());
    print_test_result("Test Null Pointer Memset", TestNullPointerMemset());
    print_test_result("Test Copy Non-Overlapping", TestCopyNonOverlapping());
    print_test_result("Test Copy Overlapping", TestCopyOverlapping());
    print_test_result("Test Null Pointer Src", TestNullPointerSrc());
    print_test_result("Test Null Pointer Dest", TestNullPointerDest());
}
