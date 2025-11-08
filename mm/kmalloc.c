#include "mm/kmalloc.h"
#include "arch/x86/include/interrupt.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "drivers/video/video.h"
#include "drivers/char/io.h"
#include <stdbool.h>
#include <stdint.h>

extern char _kernel_end; // Defined by the linker script

size_t total_memory = 0;

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

memory_block* free_list = NULL;
static spinlock_t heap_lock = SPINLOCK_INIT;  // Protect heap operations


void print_memory_size(uint64_t total_memory) {
    uint64_t total_kb = total_memory / (uint64_t)1024;
    uint64_t total_mb = total_kb / (uint64_t)1024;

    printf("**********Total System Memory**********: %d MB\n", (int)total_mb);
}

// Frame management functions (must be before initialize_memory_system)
void set_frame(size_t frame) {
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}

void clear_frame(size_t frame) {
    frame_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

int test_frame(size_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

void initialize_memory_system() {
    if (total_memory == 0) {
        printf("Error: total_memory not initialized.\n");
        return;
    }

    uintptr_t memory_end = 0x1FDFFFFF; //0x1FDFFFFF; // 511MB
    // setup the stack
    uint32_t stack_size = 1024 * 8;
    uint32_t* stack_start = (uint32_t*)(&_kernel_end - stack_size);
    uint32_t stack_end = HEAP_END;

    printf("Kennel end: %p\n", &_kernel_end);

    printf("Setting stack pointer to: %p\n", stack_start);
    // asm volatile("cli");
    // asm volatile("mov %0, %%esp" :: "r"(stack_start));
    // asm volatile("sti");
    

    // Initialize the heap

    void* heap_start = (void*)ALIGN_UP((size_t)&_kernel_end, 16);
    void* heap_end = (void*)HEAP_END;

    // Calculate frame bitmap size
    size_t bitmap_size = (total_memory / FRAME_SIZE + 7) / 8; // Rounded up
    
    // Align bitmap size to 16 bytes
    bitmap_size = ALIGN_UP(bitmap_size, 16);
    
    // Place frame bitmap at the start of heap
    frame_bitmap = (uint8_t*)heap_start;
    memset(frame_bitmap, 0, bitmap_size);
    
    // Mark frame 0 as used (NULL pointer protection - contains IVT and BIOS data)
    set_frame(0);
    
    // Place free_list AFTER the frame bitmap
    void* freelist_start = (void*)((size_t)heap_start + bitmap_size);
    free_list = (memory_block*)freelist_start;
    free_list->size = (size_t)heap_end - (size_t)freelist_start - BLOCK_SIZE;
    free_list->free = 1;
    free_list->next = NULL;

    print_memory_size(total_memory);
    printf("Frame bitmap: %p - %p (%u bytes)\n", frame_bitmap, (void*)((size_t)frame_bitmap + bitmap_size), (unsigned int)bitmap_size);
    printf("Heap Range: %p - %p\n", freelist_start, heap_end);
}

size_t allocate_frame() {
    size_t max_frames = total_memory / FRAME_SIZE;
    size_t used_frames = 0;
    
    // Start from frame 1 to avoid returning NULL (frame 0 = address 0x0)
    // Frame 0 contains real-mode IVT and BIOS data area, should never be used
    for (size_t i = 1; i < max_frames; i++) {
        if (!test_frame(i)) {
            set_frame(i);
            return i * FRAME_SIZE;
        }
        used_frames++;
    }
    
    // No free frame - print diagnostics
    printf("[CRITICAL] Frame allocation failed: %u/%u frames used (%u KB / %u KB)\n",
           used_frames, max_frames, 
           (used_frames * FRAME_SIZE) / 1024, 
           (max_frames * FRAME_SIZE) / 1024);
    
    return 0; // No free frame
}

void free_frame(size_t addr) {
    size_t frame = addr / FRAME_SIZE;
    clear_frame(frame);
}

void* k_malloc(size_t size) {
    uint32_t flags = spinlock_acquire_irq(&heap_lock);
    
    memory_block* current = free_list;

    while (current) {
        if (current->free && current->size >= size) {
            current->free = 0;

            if (current->size > size + BLOCK_SIZE) {
                memory_block* new_block = (memory_block*)((char*)current + BLOCK_SIZE + size);
                new_block->size = current->size - size - BLOCK_SIZE;
                new_block->free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }
            
            spinlock_release_irq(&heap_lock, flags);
            return (void*)((char*)current + BLOCK_SIZE);
        }
        current = current->next;
    }

    void* new_heap_block = (void*)allocate_frame();
    if (!new_heap_block) {
        printf("Out of memory (failed to allocate frame for %u bytes)\n", size);
        spinlock_release_irq(&heap_lock, flags);
        return NULL;
    }

    current = (memory_block*)new_heap_block;
    current->size = FRAME_SIZE - BLOCK_SIZE;
    current->free = 1;
    current->next = NULL;

    memory_block* last = free_list;
    while (last->next) {
        last = last->next;
    }
    last->next = current;

    spinlock_release_irq(&heap_lock, flags);
    
    // Recursive call - will acquire lock again
    return k_malloc(size);
}

void k_free(void* ptr) {
    if (!ptr) return;

    uint32_t flags = spinlock_acquire_irq(&heap_lock);
    
    memory_block* block = (memory_block*)((char*)ptr - BLOCK_SIZE);
    
    // Double-free detection
    if (block->free) {
        printf("Warning: Double free detected at %p\n", ptr);
        spinlock_release_irq(&heap_lock, flags);
        return;
    }
    
    block->free = 1;

    if (block->next && block->next->free) {
        block->size += block->next->size + BLOCK_SIZE;
        block->next = block->next->next;
    }

    memory_block* current = free_list;
    while (current) {
        if (current->next == block && current->free) {
            current->size += block->size + BLOCK_SIZE;
            current->next = block->next;
            break;
        }
        current = current->next;
    }
    
    spinlock_release_irq(&heap_lock, flags);
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

//---------------------------------------------------------------------------------------------
#define LINE_WIDTH 80

void print_test_result(const char *test_name, bool passed) {
    // Use ANSI codes for serial console + VGA colors
    if (passed) {
        printf("  \x1B[32m✓\x1B[0m %s\n", test_name);  // Green checkmark
    } else {
        printf("  \x1B[31m✗\x1B[0m %s\n", test_name);  // Red X
    }
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

bool test_reset_after_free() {
    void *first_ptr = k_malloc(1);
    if (!first_ptr) return false;

    k_free(first_ptr);

    void *second_ptr = k_malloc(1);
    return first_ptr == second_ptr;
}

bool test_multiple_frees() {
    k_free(NULL);
    k_free(NULL);
    void *ptr = k_malloc(1);
    return ptr != NULL;
}

bool test_set_memory() {
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

bool test_set_zero() {
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

bool test_null_pointer_memset() {
    return memset(NULL, 0, 10) == NULL;
}

bool test_copy_non_overlapping() {
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

bool test_copy_overlapping() {
    char buffer[20] = "123456789";
    memcpy(buffer + 4, buffer, 10);

    for (int i = 0; i < 10; i++) {
        if (buffer[i + 4] != buffer[i]) {
            return false;
        }
    }
    return true;
}

bool test_null_pointer_src() {
    char dest[10];
    return memcpy(dest, NULL, 10) == NULL;
}

bool test_null_pointer_dest() {
    char src[10] = "123456789";
    return memcpy(NULL, src, 10) == NULL;
}

void test_malloc() {
    // Basic allocation test (silent)
    void* ptr1 = k_malloc(1024);
    void* ptr2 = k_malloc(2048);
    k_free(ptr1);
    void* ptr3 = k_malloc(512);
    // Silent - tests will report via test_memory()
}


void test_memory() {
    printf("Memory Tests:\n");
    
    int passed = 0;
    int total = 10;
    
    bool result;
    
    result = test_realloc();
    print_test_result("Realloc", result);
    if (result) passed++;
    
    result = test_reset_after_free();
    print_test_result("Reset After Free", result);
    if (result) passed++;
    
    result = test_multiple_frees();
    print_test_result("Multiple Frees", result);
    if (result) passed++;
    
    result = test_set_memory();
    print_test_result("Set Memory", result);
    if (result) passed++;
    
    result = test_set_zero();
    print_test_result("Set Zero", result);
    if (result) passed++;
    
    result = test_null_pointer_memset();
    print_test_result("Null Pointer Memset", result);
    if (result) passed++;
    
    result = test_copy_non_overlapping();
    print_test_result("Copy Non-Overlapping", result);
    if (result) passed++;
    
    result = test_copy_overlapping();
    print_test_result("Copy Overlapping", result);
    if (result) passed++;
    
    result = test_null_pointer_src();
    print_test_result("Null Pointer Src", result);
    if (result) passed++;
    
    result = test_null_pointer_dest();
    print_test_result("Null Pointer Dest", result);
    if (result) passed++;
    
    if (passed == total) {
        printf("\x1B[32mAll tests passed (%d/%d)\x1B[0m\n\n", passed, total);
    } else {
        printf("\x1B[31mSome tests failed (%d/%d passed)\x1B[0m\n\n", passed, total);
    }
}
