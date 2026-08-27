/**
 * @file include/lib/spinlock.h
 * @brief Begrenzter IRQ- und SMP-sicherer Spinlock-Vertrag.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Lockbesitz und Restore-Flags dürfen nicht zwischen Kontexten vertauscht werden.
 */
#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"

/**
 * @file spinlock.h
 * @brief IRQ-safe mutual exclusion for the bounded SMP kernel
 * 
 * Ownership is recorded by logical CPU index. Acquisition has a fixed retry
 * ceiling: a wedged owner therefore becomes a diagnosed fail-closed event
 * instead of an unbounded kernel hang. Use for:
 * - Short critical sections
 * - Interrupt-safe locking (no sleeping)
 * - Protecting shared data structures
 */

typedef struct {
    volatile uint32_t lock;
    volatile uint32_t owner_cpu;
} spinlock_t;

#define SPINLOCK_NO_OWNER X86_CPU_INDEX_INVALID
#define SPINLOCK_ACQUIRE_SPIN_LIMIT (1U << 20U)

/**
 * Static initializer for spinlock (use at declaration)
 * Example: static spinlock_t my_lock = SPINLOCK_INIT;
 */
#define SPINLOCK_INIT { .lock = 0U, .owner_cpu = SPINLOCK_NO_OWNER }

/**
 * Initialize a spinlock to unlocked state
 * @param lock Pointer to spinlock structure
 */
static inline void spinlock_init(spinlock_t *lock) {
    KASSERT(lock != NULL);
    lock->owner_cpu = SPINLOCK_NO_OWNER;
    __sync_lock_release(&lock->lock);
}

/** Attempt a finite acquisition with IRQs already disabled. */
static inline bool spinlock_acquire_bounded(spinlock_t *lock,
                                            uint32_t spin_limit) {
    KASSERT(lock != NULL);
    KASSERT_IRQ_DISABLED();
    KASSERT(spin_limit != 0U);
    uint32_t cpu = x86_cpu_current_index();
    KASSERT(cpu != X86_CPU_INDEX_INVALID);
    if (lock->owner_cpu == cpu) {
        uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
        panic_context_set("synchronization", "SMP spinlock", "acquire",
                          "recursive owner");
        /* Text addresses fit in the low 24 bits of the bounded 32-bit kernel
         * image.  Preserve the CPU index in the high byte so a field panic
         * identifies both the recursive call site and its execution CPU. */
        panic_context_set_result(-35, (uint32_t)(uintptr_t)lock,
                                 (cpu << 24U) | (caller & 0x00FFFFFFU));
        panic("Recursive SMP spinlock acquisition");
    }

    for (uint32_t spin = 0U; spin < spin_limit; ++spin) {
        if (__sync_bool_compare_and_swap(&lock->lock, 0U, 1U)) {
            lock->owner_cpu = cpu;
            __sync_synchronize();
            return true;
        }
        __asm__ __volatile__("pause");
    }
    return false;
}

/**
 * Acquire a spinlock (busy-wait until available)
 * @param lock Pointer to spinlock structure
 * 
 * Uses GCC atomic built-in for test-and-set operation.
 * Includes PAUSE instruction to reduce contention.
 */
static inline void spinlock_acquire(spinlock_t *lock) {
    if (!spinlock_acquire_bounded(lock, SPINLOCK_ACQUIRE_SPIN_LIMIT)) {
        panic_context_set("synchronization", "SMP spinlock", "acquire",
                          "bounded owner wait");
        panic_context_set_result(-110, lock->owner_cpu,
                                 SPINLOCK_ACQUIRE_SPIN_LIMIT);
        panic("SMP spinlock acquisition timed out");
    }
}

/**
 * Release a spinlock
 * @param lock Pointer to spinlock structure
 * 
 * Uses GCC atomic built-in for release operation with memory barrier.
 */
static inline void spinlock_release(spinlock_t *lock) {
    KASSERT(lock != NULL);
    KASSERT_IRQ_DISABLED();
    KASSERT(lock->lock != 0);
    uint32_t cpu = x86_cpu_current_index();
    KASSERT(cpu != X86_CPU_INDEX_INVALID);
    KASSERT(lock->owner_cpu == cpu);
    __sync_synchronize();
    lock->owner_cpu = SPINLOCK_NO_OWNER;
    __sync_lock_release(&lock->lock);
}

/**
 * Try to acquire spinlock without blocking
 * @param lock Pointer to spinlock structure
 * @return 1 if lock acquired, 0 if lock already held
 */
static inline int spinlock_trylock(spinlock_t *lock) {
    KASSERT(lock != NULL);
    KASSERT_IRQ_DISABLED();
    uint32_t cpu = x86_cpu_current_index();
    KASSERT(cpu != X86_CPU_INDEX_INVALID);
    KASSERT(lock->owner_cpu != cpu);
    if (!__sync_bool_compare_and_swap(&lock->lock, 0U, 1U)) return 0;
    lock->owner_cpu = cpu;
    __sync_synchronize();
    return 1;
}

/**
 * Check if spinlock is currently held
 * @param lock Pointer to spinlock structure
 * @return 1 if locked, 0 if unlocked
 * 
 * WARNING: This is a hint only - lock state can change immediately after check
 */
static inline int spinlock_is_locked(const spinlock_t *lock) {
    KASSERT(lock != NULL);
    return lock->lock != 0;
}

/** Return whether the calling logical CPU currently owns the lock. */
static inline bool spinlock_is_owned_by_current(const spinlock_t *lock) {
    if (lock == NULL || lock->lock == 0U) return false;
    uint32_t cpu = x86_cpu_current_index();
    return cpu != X86_CPU_INDEX_INVALID && lock->owner_cpu == cpu;
}

//=============================================================================
// Interrupt-Safe Locking
//=============================================================================

/**
 * Acquire spinlock with interrupt disabling
 * @param lock Pointer to spinlock structure
 * @return Previous interrupt state (pass to spinlock_release_irq)
 * 
 * Use pattern:
 *   uint32_t flags = spinlock_acquire_irq(&lock);
 *   // critical section
 *   spinlock_release_irq(&lock, flags);
 */
static inline uint32_t spinlock_acquire_irq(spinlock_t *lock) {
    uint32_t flags = irq_save();
    spinlock_acquire(lock);
    return flags;
}

/**
 * Release spinlock and restore interrupt state
 * @param lock Pointer to spinlock structure
 * @param flags Interrupt state from spinlock_acquire_irq()
 */
static inline void spinlock_release_irq(spinlock_t *lock, uint32_t flags) {
    spinlock_release(lock);
    irq_restore(flags);
}

#endif // SPINLOCK_H
