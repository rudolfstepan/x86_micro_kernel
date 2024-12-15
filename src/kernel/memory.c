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

volatile size_t total_memory; // in bytes set by parsing the multiboot memory map after kernel initialization

#define HEAP_START ((uint32_t)(&_kernel_end))
#define HEAP_END ((uint32_t)(0x0f000000)) // End of heap (5MB)
#define ALIGN_UP(addr, align) (((addr) + ((align)-1)) & ~((align)-1))
#define HEAP_START_ALIGNED ALIGN_UP((size_t)HEAP_START, 16)
#define BLOCK_SIZE sizeof(memory_block)

#define E820_BUFFER_SIZE 128
#define FRAME_SIZE 4096 // 4KB
#define MAX_FRAMES (512 * 1024 * 1024 / FRAME_SIZE) // For 512MB RAM

uint8_t* frame_bitmap; // Dynamically allocate based on total_memory

typedef struct {
    uint64_t base_addr; // Base address of the memory region
    uint64_t length;    // Length of the memory region
    uint32_t type;      // Type of the memory region
    uint32_t acpi;      // ACPI attributes
} __attribute__((packed)) e820_entry_t;

typedef struct memory_block {
    size_t size;
    int free;
    struct memory_block* next;
} memory_block;

memory_block* freeList = NULL;


void initialize_memory_system() {
    if (total_memory == 0) {
        printf("Error: total_memory not initialized.\n");
        return;
    }

    size_t heap_size = HEAP_END - (size_t)&_kernel_end;
    printf("Kernel end: %p\n", &_kernel_end);
    printf("Heap Range: %p - %p\n", (void*)HEAP_START_ALIGNED, (void*)HEAP_END);

    // Dynamically allocate frame bitmap
    size_t bitmap_size = ALIGN_UP((total_memory / FRAME_SIZE + 7) / 8, 16);
    frame_bitmap = (uint8_t*)HEAP_START_ALIGNED;

    if ((size_t)frame_bitmap + bitmap_size > HEAP_END) {
        printf("Error: Not enough memory for frame bitmap.\n");
        return;
    }
    memset(frame_bitmap, 0, bitmap_size);

    // Adjust freeList to exclude the bitmap
    void* heap_start = (void*)((size_t)frame_bitmap + bitmap_size);
    freeList = (memory_block*)ALIGN_UP((size_t)heap_start, 16);

    freeList->size = HEAP_END - (size_t)freeList - BLOCK_SIZE;
    freeList->free = 1;
    freeList->next = NULL;

    printf("Initialized memory system. Total memory: %d MB\n", total_memory / 1024 / 1024);
}

void set_frame(size_t frame) {
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}

void clear_frame(size_t frame) {
    frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

int test_frame(size_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

size_t allocate_frame() {
    for (size_t i = 0; i < total_memory / FRAME_SIZE; i++) {
        if (!test_frame(i)) {
            set_frame(i);
            return i * FRAME_SIZE;
        }
    }
    printf("Error: No free frames available.\n");
    return (size_t)-1; // Indicate failure
}

void free_frame(size_t addr) {
    size_t frame = addr / FRAME_SIZE;
    clear_frame(frame);
}

void* k_malloc(size_t size) {
    size = ALIGN_UP(size, 16); // Ensure alignment
    memory_block* current = freeList;

    while (current) {
        if (current->free && current->size >= size) {
            current->free = 0;

            if (current->size > size + BLOCK_SIZE) {
                memory_block* newBlock = (memory_block*)((char*)current + BLOCK_SIZE + size);
                newBlock->size = current->size - size - BLOCK_SIZE;
                newBlock->free = 1;
                newBlock->next = current->next;

                current->size = size;
                current->next = newBlock;
            }
            return (void*)((char*)current + BLOCK_SIZE);
        }
        current = current->next;
    }

    printf("Error: Out of heap memory.\n");
    return NULL;
}

void k_free(void* ptr) {
    if (!ptr) return;

    memory_block* block = (memory_block*)((char*)ptr - BLOCK_SIZE);
    if (block->free) {
        printf("Warning: Double free detected.\n");
        return;
    }

    block->free = 1;

    // Merge with the next block if free
    if (block->next && block->next->free) {
        block->size += block->next->size + BLOCK_SIZE;
        block->next = block->next->next;
    }

    // Merge with the previous block if free
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

void* k_realloc(void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return k_malloc(new_size);
    }

    if (new_size == 0) {
        k_free(ptr);
        return NULL;
    }

    memory_block* block = (memory_block*)((char*)ptr - BLOCK_SIZE);
    size_t old_size = block->size;

    if (new_size <= old_size) {
        return ptr;
    }

    void* new_ptr = k_malloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memmove(new_ptr, ptr, copy_size);

    k_free(ptr);

    return new_ptr;
}

void show_memory_map() {
    printf("\n--- Memory Map (Occupied Only) ---\n");

    // Display the occupied heap memory blocks
    printf("Heap (Occupied Blocks):\n");
    memory_block* current = freeList;
    while (current) {
        if (!current->free) {
            printf("  Block at %p: Size: %u bytes\n", current, current->size);
        }
        current = current->next;
    }

    // Display the occupied frames
    printf("\nFrames (Occupied):\n");
    size_t total_frames = total_memory / FRAME_SIZE;
    size_t used_frames = 0;

    for (size_t i = 0; i < total_frames; i++) {
        if (test_frame(i)) {
            if (used_frames % 8 == 0) printf("\n  "); // Group occupied frames for readability
            printf("Frame %u ", i);
            used_frames++;
        }
    }

    printf("\n\nTotal Used Frames: %u\n", used_frames);
    printf("-------------------\n");
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
    void *ptr = k_malloc(10);
    if (!ptr) return false;

    ptr = k_realloc(ptr, 20);
    if (!ptr) return false;

    ptr = k_realloc(ptr, 5);
    if (!ptr) return false;

    k_free(ptr);
    return true;
}

bool TestResetAfterFree() {
    void *firstPtr = k_malloc(1);
    if (!firstPtr) return false;

    k_free(firstPtr);

    void *secondPtr = k_malloc(1);
    return firstPtr == secondPtr;
}

bool TestMultipleFrees() {
    k_free(NULL);
    k_free(NULL);
    void *ptr = k_malloc(1);
    return ptr != NULL;
}

bool TestSetMemory() {
    char *buffer = (char *)k_malloc(10);
    if (!buffer) return false;

    memset(buffer, 'A', 10);
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 'A') {
            k_free(buffer);
            return false;
        }
    }

    k_free(buffer);
    return true;
}

bool TestSetZero() {
    char *buffer = (char *)k_malloc(10);
    if (!buffer) return false;

    memset(buffer, 0, 10);
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 0) {
            k_free(buffer);
            return false;
        }
    }

    k_free(buffer);
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
    k_free(ptr1);
    void* ptr3 = k_malloc(512);
    printf("Tests complete: ptr1=%p, ptr2=%p, ptr3=%p\n", ptr1, ptr2, ptr3);

    k_free(ptr2);
    k_free(ptr3);
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
