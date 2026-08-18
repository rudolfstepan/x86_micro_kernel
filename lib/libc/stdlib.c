/**
 * @file lib/libc/stdlib.c
 * @brief Freestanding Konvertierung und Basis-Laufzeitfunktionen.
 *
 * Layer: Freestanding kernel/userspace runtime.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#include <stddef.h>
#include <stdint.h>

#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "drivers/video/video.h"
#include "kernel/time/pit.h"
#include "mm/kmalloc.h"

//TryContext* current_try_context = NULL;

void* malloc(size_t size) {
    void* allocated_memory = k_malloc(size);

    if (allocated_memory) {
        //printf("Memory allocated at: %p\n", allocated_memory);
    } else {
        set_color(RED);
        printf("Memory allocation failed.\n");
        set_color(WHITE);
    }

    return allocated_memory;
}

/**
 * Allokiert Speicher mit der angegebenen Ausrichtung.
 *
 * @param alignment Die gewünschte Speicher-Ausrichtung (muss eine Zweierpotenz sein).
 * @param size Die gewünschte Größe des Speichers in Bytes.
 * @return Ein Zeiger auf den ausgerichteten Speicher oder NULL bei Fehler.
 */
void* aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        // Alignment ist keine Zweierpotenz
        return NULL;
    }

    // Allokiere zusätzlichen Speicher für Alignment und Speicheradresse
    if (alignment - 1U > SIZE_MAX - sizeof(void*)) {
        return NULL;
    }
    size_t overhead = alignment - 1U + sizeof(void*);
    if (size > SIZE_MAX - overhead) {
        return NULL;
    }
    size_t total_size = size + overhead;
    void* raw_memory = malloc(total_size);
    if (!raw_memory) {
        return NULL; // Speicher konnte nicht allokiert werden
    }

    // Berechne die ausgerichtete Adresse
    uintptr_t raw_address = (uintptr_t)raw_memory + sizeof(void*);
    uintptr_t aligned_address = (raw_address + alignment - 1) & ~(alignment - 1);

    // Speichere den Originalzeiger vor der ausgerichteten Adresse
    ((void**)aligned_address)[-1] = raw_memory;

    return (void*)aligned_address;
}

/**
 * Gibt den von aligned_alloc zugewiesenen Speicher frei.
 *
 * @param ptr Ein Zeiger, der von aligned_alloc zurückgegeben wurde.
 */
void aligned_free(void* ptr) {
    if (ptr) {
        // Hole den Originalzeiger und gib den Speicher frei
        free(((void**)ptr)[-1]);
    }
}













void* realloc(void *ptr, size_t new_size) {
    return k_realloc(ptr, new_size);
}

void free(void* ptr) {
    if (!ptr) return;

    k_free(ptr);
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

// Function to halt the CPU
void exit(int status) {
    syscall(SYS_EXIT, (void*)(uintptr_t)status, NULL, NULL);
    for (;;) {
        /* SYS_EXIT is noreturn; remain safe if a broken kernel returns. */
    }
}

void delay_ms(uint32_t ms) {
    /* This libc is linked into the kernel.  Hardware/early-boot delays must
     * not enter INT 0x80 and accidentally join a userspace sleep queue while
     * a process happens to be current.  Ring-3 programs use x86os_delay() or
     * x86os_sleep_ms() from the userspace SDK instead. */
    pit_delay(ms);
}

void wait_enter_pressed() {
	syscall(SYS_WAIT_ENTER, NULL, NULL, NULL);
}

// Throw an exception
// void throw(TryContext* ctx, int exception_code) {
//     if (ctx) {
//         ctx->exception_code = exception_code;

//         // Debugging: Print the context before jumping
//         // printf("Throwing exception with code: %d\n", exception_code);
//         // printf("Current throw context: ESP=0x%X, EBP=0x%X, EIP=0x%X\n",
//         //        ctx->esp, ctx->ebp, ctx->eip);

//         longjmp(ctx, exception_code); // Call longjmp to restore context
//     }
// }

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


// Disable interrupts
void disable_interrupts() {
    __asm__ volatile ("cli");
}

// Enable interrupts
void enable_interrupts() {
    __asm__ volatile ("sti");
}

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) {
        // Handle division by zero (could be kernel panic or error handling)
        while (1) {} // Infinite loop to halt execution
    }

    if (dividend < divisor) {
        return 0; // Result is 0 if divisor > dividend
    }

    uint64_t quotient = 0;
    uint64_t remainder = 0;

    // Perform the division using bit manipulation
    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1); // Shift in next bit
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1ULL << i); // Set the corresponding bit in the quotient
        }
    }

    return quotient;
}

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
    if (divisor == 0) {
        // Division by zero is undefined; handle appropriately.
        // For example, halt the system or return 0.
        while (1); // Infinite loop to signal an error
    }

    // If the divisor is greater than the dividend, the result is the dividend
    if (dividend < divisor) {
        return dividend;
    }

    // Perform manual division to calculate the remainder
    uint64_t remainder = 0;

    // Iterate over each bit of the dividend (64 bits)
    for (int i = 63; i >= 0; i--) {
        // Shift the remainder left by 1 and bring down the next bit from the dividend
        remainder = (remainder << 1) | ((dividend >> i) & 1);

        // Subtract the divisor if the remainder is greater or equal to the divisor
        if (remainder >= divisor) {
            remainder -= divisor;
        }
    }

    return remainder;
}

// Convert string to integer
int atoi(const char* str) {
    if (!str) return 0;
    
    int result = 0;
    int sign = 1;
    int i = 0;
    
    // Skip leading whitespace
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
        i++;
    }
    
    // Handle sign
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }
    
    // Convert digits
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    
    return sign * result;
}
