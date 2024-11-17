#include "memory.h"

#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
#include <stdbool.h>


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

    //printf("Aligned HEAP_START: 0x%p\n", freeList);
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
#define LINE_WIDTH 80

void print_test_result(const char *test_name, bool passed) {
    int name_length = strlen(test_name);
    int padding = LINE_WIDTH - name_length - 7; // 7 for " [ OK ]" or " [FAILED]"
    printf("%s%*s\n", test_name, padding, passed ? "[ OK ]" : "[FAILED]");
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

void test_memory() {
    printf("Testing Memory...\n");
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
    printf("Testing Complete.\n");
}
