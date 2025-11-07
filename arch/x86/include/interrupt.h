#ifndef ARCH_X86_INTERRUPT_H
#define ARCH_X86_INTERRUPT_H

/**
 * x86 Interrupt Management
 * 
 * Provides inline functions for controlling x86 interrupt flag (IF).
 * These are critical for implementing atomic operations and critical sections.
 */

#include <stdint.h>

/**
 * Disable interrupts (CLI)
 * Sets IF=0 in EFLAGS, preventing hardware interrupts
 */
static inline void irq_disable(void) {
    __asm__ __volatile__("cli" ::: "memory");
}

/**
 * Enable interrupts (STI)
 * Sets IF=1 in EFLAGS, allowing hardware interrupts
 */
static inline void irq_enable(void) {
    __asm__ __volatile__("sti" ::: "memory");
}

/**
 * Check if interrupts are enabled
 * @return 1 if IF=1, 0 if IF=0
 */
static inline int irq_enabled(void) {
    uint32_t flags;
    __asm__ __volatile__(
        "pushf\n"
        "pop %0\n"
        : "=r"(flags)
        :: "memory"
    );
    return (flags & 0x200) != 0;  // Bit 9 is IF
}

/**
 * Save current interrupt state and disable interrupts
 * @return EFLAGS value (use with irq_restore)
 * 
 * Usage:
 *   uint32_t flags = irq_save();
 *   // Critical section...
 *   irq_restore(flags);
 */
static inline uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ __volatile__(
        "pushf\n"
        "pop %0\n"
        "cli\n"
        : "=r"(flags)
        :: "memory"
    );
    return flags;
}

/**
 * Restore interrupt state saved by irq_save
 * @param flags EFLAGS value from irq_save()
 */
static inline void irq_restore(uint32_t flags) {
    __asm__ __volatile__(
        "push %0\n"
        "popf\n"
        :: "r"(flags)
        : "memory", "cc"
    );
}

/**
 * Halt CPU until next interrupt (HLT)
 * Used in idle loops to save power
 */
static inline void cpu_halt(void) {
    __asm__ __volatile__("hlt");
}

/**
 * Infinite halt loop (CLI + HLT)
 * Used after unrecoverable errors
 */
static inline void cpu_halt_forever(void) {
    irq_disable();
    while (1) {
        cpu_halt();
    }
}

#endif // ARCH_X86_INTERRUPT_H
