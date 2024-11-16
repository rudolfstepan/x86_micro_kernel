#include <stddef.h>
#include <stdint.h>

#include "stdlib.h"
#include "stdio.h"
#include "strings.h"

TryContext* current_try_context = NULL;


typedef struct memory_block {
    size_t size;
    int free;
    struct memory_block *next;
} memory_block;

// Kernel starts at 0x100000 1MB address in memory
// Heap starts at 0x100000 + Kernel Size
// TODO: Get the kernel size from the linker script and use the memory addresses dynamically from the bootloader
#define HEAP_START  (void*)(0x100000 + (1024 * 1024))  // 2MB
#define HEAP_END    (void*)0x500000  // 5MB
#define BLOCK_SIZE sizeof(memory_block)

memory_block *freeList = (memory_block*)HEAP_START;

void initialize_memory_system() {
    // Initialize memory pool on first malloc call
    freeList->size = (size_t)HEAP_END - (size_t)HEAP_START - BLOCK_SIZE;
    freeList->free = 1;
    freeList->next = NULL;
}

void *malloc(size_t size) {
    memory_block *current = freeList, *prev = NULL;
    if(prev == NULL) {
        
    }
    void *result = NULL;

    // Align size to word boundary
    size = (size + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);

    while (current) {
        // Check if current block is free and large enough
        if (current->free && current->size >= size) {
            // Check if we can split the block
            if (current->size > size + BLOCK_SIZE + sizeof(size_t)) {
                memory_block *newBlock = (memory_block*)((size_t)current + BLOCK_SIZE + size);
                newBlock->size = current->size - size - BLOCK_SIZE;
                newBlock->free = 1;
                newBlock->next = current->next;
                current->size = size;
                current->next = newBlock;
            }
            current->free = 0;
            result = (void*)((size_t)current + BLOCK_SIZE);
            break;
        }
        prev = current;
        current = current->next;
    }

    return result;
}

void* realloc(void *ptr, size_t new_size) {
    // If `ptr` is NULL, just allocate new memory like `malloc`
    if (ptr == NULL) {
        return malloc(new_size);
    }

    // If `new_size` is 0, free the memory and return NULL
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }

    // Get the memory block header by moving back from `ptr`
    memory_block *block = (memory_block*)((size_t)ptr - BLOCK_SIZE);
    size_t old_size = block->size;

    // If the new size is the same or smaller, return the original pointer
    if (new_size <= old_size) {
        return ptr;
    }

    // Allocate a new block of memory large enough for the new size
    void *new_ptr = malloc(new_size);
    if (!new_ptr) {
        // If allocation fails, return NULL
        return NULL;
    }

    // Copy data from the old memory to the new memory
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Free the old memory block
    free(ptr);

    // Return the pointer to the new memory block
    return new_ptr;
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    memory_block *block = (memory_block*)((size_t)ptr - BLOCK_SIZE);
    block->free = 1;

    // Coalescing free blocks
    memory_block *current = freeList;
    while (current) {
        if (current->free && current->next && current->next->free) {
            current->size += current->next->size + BLOCK_SIZE;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

void secure_free(void *ptr, size_t size) {
    if (ptr != NULL) {
        memset(ptr, 0, size);
        free(ptr);
    }
}


// test methods

void TestResetAfterFree() {
    void* firstPtr = malloc(1);
    //printf("First allocation: %p\n", firstPtr);

    free(firstPtr); // This should store the initial alignment padding

    void* secondPtr = malloc(1);
    //printf("Second allocation: %p\n", secondPtr);

    if (firstPtr == secondPtr) {
        //printf("TestResetAfterFree: Passed\n");
    } else {
        printf("TestResetAfterFree: Failed. Expected: %p, Got: %p\n", firstPtr, secondPtr);
    }
}

void TestMultipleFrees() {
    free(NULL);
    free(NULL);
    void* ptr = malloc(1);

    if (ptr != NULL) {
        //printf("TestMultipleFrees: Passed\n");
    } else {
        printf("TestMultipleFrees: Failed\n");
    }
}
void TestSetMemory() {
    char* buffer = (char*)malloc(10);
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
    char* buffer = (char*)malloc(10);
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
    printf("Testing Memory...");
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

void* memmove(void* dest, const void* src, size_t n) {
    // Cast to unsigned char pointers for byte-wise operations
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0) {
        // No need to move if source and destination are the same or if size is 0
        return dest;
    }

    if (d < s || d >= (s + n)) {
        // Non-overlapping, safe to copy forward
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        // Overlapping, copy backward to prevent overwriting source
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dest;
}

void sys_call(int syscall_index, int parameter1, int parameter2, int parameter3) {
    __asm__ volatile(
        "int $0x80\n"       // Trigger syscall interrupt
        : // No output
        : "a"(syscall_index), "b"(parameter1), "c"(parameter2), "d"(parameter3)
        : "memory"
    );
}

// Function to halt the CPU
void exit(uint8_t status) {
    // Assembly code to halt the CPU
    //__asm__ __volatile__("cli; hlt");
}

void sleep_ms(uint32_t ms) {
    sys_call(SYS_DELAY, ms, 0, 0);
}

void wait_enter_pressed() {
	sys_call(SYS_WAIT_ENTER, 0, 0, 0);
}

// Throw an exception
void throw(TryContext* ctx, int exception_code) {
    ctx->exception_code = exception_code; // Set the exception code in the context
    longjmp(ctx); // Call the custom longjmp with one argument
}