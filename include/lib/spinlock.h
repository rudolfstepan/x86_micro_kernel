/**
 * @file include/lib/spinlock.h
 * @brief IRQ-sicherer Spinlock-Vertrag.
 *
 * Layer: Ring-0 x86 architecture and memory.
 * Contract: Binärlayouts, Adressgrenzen und Privilegien entsprechen der x86-Hardware-ABI.
 * Safety: Lockbesitz und Restore-Flags dürfen nicht zwischen Kontexten vertauscht werden.
 */
#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"

/**
 * @file spinlock.h
 * @brief IRQ-safe mutual exclusion for the current uniprocessor kernel
 * 
 * Atomic instructions keep the representation SMP-ready, but recursive-acquire
 * detection currently assumes one active CPU.  Revisit ownership tracking
 * before AP startup. Use for:
 * - Short critical sections
 * - Interrupt-safe locking (no sleeping)
 * - Protecting shared data structures
 */

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

/**
 * Static initializer for spinlock (use at declaration)
 * Example: static spinlock_t my_lock = SPINLOCK_INIT;
 */
#define SPINLOCK_INIT { .lock = 0 }

/**
 * Initialize a spinlock to unlocked state
 * @param lock Pointer to spinlock structure
 */
static inline void spinlock_init(spinlock_t *lock) {
    lock->lock = 0;
}

/**
 * Acquire a spinlock (busy-wait until available)
 * @param lock Pointer to spinlock structure
 * 
 * Uses GCC atomic built-in for test-and-set operation.
 * Includes PAUSE instruction to reduce contention.
 */
static inline void spinlock_acquire(spinlock_t *lock) {
    KASSERT(lock != NULL);
    KASSERT_IRQ_DISABLED();
    KASSERT(lock->lock == 0);
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        // CPU hint: we're spinning (reduces power, improves performance)
        __asm__ __volatile__("pause");
    }
    // Memory barrier: ensure all loads/stores after lock are not reordered before
    __asm__ __volatile__("" ::: "memory");
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
    // Memory barrier: ensure all loads/stores before unlock complete
    __asm__ __volatile__("" ::: "memory");
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
    return !__sync_lock_test_and_set(&lock->lock, 1);
}

/**
 * Check if spinlock is currently held
 * @param lock Pointer to spinlock structure
 * @return 1 if locked, 0 if unlocked
 * 
 * WARNING: This is a hint only - lock state can change immediately after check
 */
static inline int spinlock_is_locked(spinlock_t *lock) {
    return lock->lock != 0;
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
