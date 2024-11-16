#include <stddef.h>
#include <stdint.h>

#include "stdlib.h"
#include "stdio.h"
#include "strings.h"


TryContext* current_try_context = NULL;



void* malloc(size_t size) {
    // perform a syscall to allocate memory
    void* allocated_memory = syscall(SYS_MALLOC, size, 0, 0); // Allocate 1024 bytes
    if (allocated_memory) {
        //printf("Memory allocated at: %p\n", allocated_memory);
    } else {
        printf("Memory allocation failed.\n");
    }

    return allocated_memory;
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

    // // Get the memory block header by moving back from `ptr`
    // memory_block *block = (memory_block*)((size_t)ptr - BLOCK_SIZE);
    // size_t old_size = block->size;

    // // If the new size is the same or smaller, return the original pointer
    // if (new_size <= old_size) {
    //     return ptr;
    // }

    // // Allocate a new block of memory large enough for the new size
    // void *new_ptr = malloc(new_size);
    // if (!new_ptr) {
    //     // If allocation fails, return NULL
    //     return NULL;
    // }

    // // Copy data from the old memory to the new memory
    // size_t copy_size = (old_size < new_size) ? old_size : new_size;
    // memcpy(new_ptr, ptr, copy_size);

    // // Free the old memory block
    // free(ptr);

    // // Return the pointer to the new memory block
    // return new_ptr;

    return NULL;
}

void free(void* ptr) {
    if (!ptr) return;

    syscall(SYS_FREE, ptr, 0, 0);
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

void* syscall(int syscall_index, int parameter1, int parameter2, int parameter3) {
    void* return_value;
    __asm__ volatile(
        "int $0x80\n"       // Trigger syscall interrupt
        : "=a"(return_value) // Output: Get return value from EAX
        : "a"(syscall_index), "b"(parameter1), "c"(parameter2), "d"(parameter3) // Inputs
        : "memory"          // Clobbers
    );
    return return_value;     // Return the value in EAX
}

// Function to halt the CPU
void exit(uint8_t status) {
    // Assembly code to halt the CPU
    //__asm__ __volatile__("cli; hlt");
}

void sleep_ms(uint32_t ms) {
    syscall(SYS_DELAY, ms, 0, 0);
}

void wait_enter_pressed() {
	syscall(SYS_WAIT_ENTER, 0, 0, 0);
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
