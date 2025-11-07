#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/**
 * @file panic.h
 * @brief Kernel panic handler for unrecoverable errors
 */

/**
 * Kernel panic - unrecoverable error
 * @param message Error message to display
 * 
 * This function never returns. It:
 * 1. Disables interrupts
 * 2. Displays error message
 * 3. Dumps CPU state if available
 * 4. Halts the system
 */
void __attribute__((noreturn)) panic(const char* message);

/**
 * Kernel assertion failure
 * @param expr Expression that failed (as string)
 * @param file Source file name
 * @param line Line number
 * @param func Function name
 */
void __attribute__((noreturn)) kassert_fail(const char* expr, const char* file, 
                                              int line, const char* func);

/**
 * Kernel assertion macro
 * Usage: KASSERT(ptr != NULL);
 */
#define KASSERT(expr) \
    do { \
        if (!(expr)) { \
            kassert_fail(#expr, __FILE__, __LINE__, __func__); \
        } \
    } while (0)

/**
 * Static (compile-time) assertion
 * Usage: STATIC_ASSERT(sizeof(int) == 4);
 */
#define STATIC_ASSERT(expr) _Static_assert(expr, #expr)

#endif // PANIC_H
