#ifndef PANIC_H
#define PANIC_H

#include <stdbool.h>
#include <stdint.h>
#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/sys.h"

bool scheduler_preempt_is_disabled(void);
bool scheduler_can_sleep(void);

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
 * Publish a bounded diagnostic breadcrumb for a later panic. All strings are
 * copied into fixed kernel storage and may be NULL. This API performs no heap
 * allocation, logging or I/O and is safe to use before a fallible operation.
 */
void panic_context_set(const char *phase, const char *component,
                       const char *operation, const char *subject);

/** Attach a result code and two subsystem-specific numeric details. */
void panic_context_set_result(int32_t result, uint32_t detail0,
                              uint32_t detail1);

/** Return the SHA-1 identifier embedded in the linked kernel image. */
const char* kernel_build_id(void);

/** Print the complete CPU state captured by an x86 exception stub. */
void panic_dump_exception_context(const Registers* registers, uint32_t cr2);

/** Kernel panic with the original CPU exception frame. */
void __attribute__((noreturn)) panic_with_exception(
    const char* message, const Registers* registers, uint32_t cr2);

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

#define KASSERT_IRQ_DISABLED() KASSERT(!irq_enabled())
#define KASSERT_NOT_IRQ() KASSERT(!irq_in_context())
#define KASSERT_CAN_SLEEP() KASSERT(scheduler_can_sleep())

/**
 * Static (compile-time) assertion
 * Usage: STATIC_ASSERT(sizeof(int) == 4);
 */
#define STATIC_ASSERT(expr) _Static_assert(expr, #expr)

#endif // PANIC_H
