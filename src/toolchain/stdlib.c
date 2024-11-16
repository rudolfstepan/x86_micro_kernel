#include <stddef.h>
#include <stdint.h>

#include "stdlib.h"
#include "stdio.h"
#include "strings.h"


TryContext* current_try_context = NULL;



void* malloc(size_t size) {
    // perform a syscall to allocate memory
    void* allocated_memory = syscall(SYS_MALLOC, (void*)size, NULL, NULL); // Allocate 1024 bytes
    if (allocated_memory) {
        //printf("Memory allocated at: %p\n", allocated_memory);
    } else {
        printf("Memory allocation failed.\n");
    }

    return allocated_memory;
}

void* realloc(void *ptr, size_t new_size) {
    return syscall(SYS_REALLOC, ptr, (void*)(uintptr_t)new_size, 0);
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

void* syscall(int syscall_index, void* parameter1, void* parameter2, void* parameter3) {
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
    syscall(SYS_DELAY, (void*)ms, NULL, NULL);
}

void wait_enter_pressed() {
	syscall(SYS_WAIT_ENTER, NULL, NULL, NULL);
}

// Throw an exception
void throw(TryContext* ctx, int exception_code) {
    if (ctx) {
        ctx->exception_code = exception_code;

        // Debugging: Print the context before jumping
        // printf("Throwing exception with code: %d\n", exception_code);
        // printf("Current throw context: ESP=0x%X, EBP=0x%X, EIP=0x%X\n",
        //        ctx->esp, ctx->ebp, ctx->eip);

        longjmp(ctx, exception_code); // Call longjmp to restore context
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
