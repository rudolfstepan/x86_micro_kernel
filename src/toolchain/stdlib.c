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

extern char _kernel_end;  // Defined by the linker script

#define HEAP_START  ((void*)(&_kernel_end))
#define HEAP_END    (void*)0x500000  // End of heap (5MB)
#define ALIGN_UP(addr, align) (((addr) + ((align)-1)) & ~((align)-1))
#define HEAP_START_ALIGNED ALIGN_UP((size_t)HEAP_START, 16)
#define BLOCK_SIZE sizeof(memory_block)

memory_block *freeList = (memory_block*)HEAP_START;

void initialize_memory_system() {
    freeList = (memory_block*)HEAP_START_ALIGNED;
    freeList->size = (size_t)HEAP_END - (size_t)HEAP_START_ALIGNED - BLOCK_SIZE;
    freeList->free = 1;
    freeList->next = NULL;

    printf("Aligned HEAP_START: 0x%p\n", freeList);
}

void* malloc(size_t size) {
    memory_block *current = freeList;

    while (current) {
        if (current->free && current->size >= size) {
            // Split the block if it's larger than required
            if (current->size > size + BLOCK_SIZE) {
                memory_block *newBlock = (memory_block*)((size_t)current + BLOCK_SIZE + size);
                newBlock->size = current->size - size - BLOCK_SIZE;
                newBlock->free = 1;
                newBlock->next = current->next;

                current->size = size;
                current->next = newBlock;
            }

            current->free = 0;  // Mark as allocated
            return (void*)((size_t)current + BLOCK_SIZE);
        }

        current = current->next;
    }

    // Out of memory
    printf("malloc: Out of memory\n");
    return NULL;
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

void free(void* ptr) {
    if (!ptr) return;

    memory_block *block = (memory_block*)((size_t)ptr - BLOCK_SIZE);
    block->free = 1;

    // Coalesce adjacent free blocks
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
    if (ctx) {
        ctx->exception_code = exception_code;

        // Debugging: Print the context before jumping
        printf("Throwing exception with code: %d\n", exception_code);
        printf("Current throw context: ESP=0x%X, EBP=0x%X, EIP=0x%X\n",
               ctx->esp, ctx->ebp, ctx->eip);

        longjmp(ctx); // Call longjmp to restore context
    }
}

uint32_t get_esp() {
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    return esp;
}

uint32_t get_ebp() {
    uint32_t ebp;
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));
    return ebp;
}
