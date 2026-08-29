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
#include "arch/x86/include/sys.h"
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
    /* Zero is unlocked; an acquired value is the owning CPU index plus one.
     * owner_cpu mirrors that token for bounded field diagnostics only. */
    volatile uint32_t lock;
    volatile uint32_t owner_cpu;
} spinlock_t;

#define SPINLOCK_NO_OWNER X86_CPU_INDEX_INVALID
#define SPINLOCK_ACQUIRE_SPIN_LIMIT (1U << 20U)
#define SPINLOCK_ACQUIRE_TIMED_SPIN_LIMIT (1U << 28U)
#define SPINLOCK_ACQUIRE_BOOT_SPIN_LIMIT SPINLOCK_ACQUIRE_SPIN_LIMIT
#define SPINLOCK_ACQUIRE_TIMEOUT_MS 250U
#define SPINLOCK_TIME_CHECK_INTERVAL 1024U
#define SPINLOCK_MIN_CPU_FREQUENCY_HZ 1000000ULL
#define SPINLOCK_MAX_CPU_FREQUENCY_HZ 10000000000ULL

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
    uint32_t owner_token = cpu + 1U;
    /* cpu_frequency is calibrated once before AP scheduling is released and
     * remains immutable afterwards.  A TSC deadline therefore gives the
     * contended runtime path a processor-speed-independent bound.  Before
     * calibration, boot is single-CPU and retains the smaller finite retry
     * ceiling instead of trusting an unavailable clock scale. */
    uint64_t frequency = cpu_frequency;
    bool elapsed_bound = frequency >= SPINLOCK_MIN_CPU_FREQUENCY_HZ &&
                         frequency <= SPINLOCK_MAX_CPU_FREQUENCY_HZ;
    uint64_t start_cycles = elapsed_bound ? cpu_cycle_counter_read() : 0U;
    uint64_t timeout_cycles = elapsed_bound
        ? (frequency / 1000U) * SPINLOCK_ACQUIRE_TIMEOUT_MS : 0U;
    for (uint32_t spin = 0U; spin < spin_limit; ++spin) {
        if (!elapsed_bound && spin >= SPINLOCK_ACQUIRE_BOOT_SPIN_LIMIT)
            return false;
        /* Test before compare-and-swap so waiters keep the cache line shared
         * while an owner is running.  This avoids a locked write attempt on
         * every PAUSE iteration and lets a preempted virtual owner resume and
         * publish release without cache-line ping-pong from all waiters. */
        uint32_t observed = lock->lock;
        if (observed == 0U) {
            observed = __sync_val_compare_and_swap(
                &lock->lock, 0U, owner_token);
        }
        if (observed == 0U) {
            lock->owner_cpu = cpu;
            __sync_synchronize();
            return true;
        }
        if (observed == owner_token) {
            uint32_t caller =
                (uint32_t)(uintptr_t)__builtin_return_address(0);
            panic_context_set("synchronization", "SMP spinlock", "acquire",
                              "recursive owner");
            /* Text addresses fit in the low 24 bits of the bounded 32-bit
             * kernel image. Preserve the CPU index in the high byte so a
             * field panic identifies both recursive call site and CPU. */
            panic_context_set_result(
                -35, (uint32_t)(uintptr_t)lock,
                (cpu << 24U) | (caller & 0x00FFFFFFU));
            panic("Recursive SMP spinlock acquisition");
        }
        __asm__ __volatile__("pause");
        if (elapsed_bound &&
            (spin & (SPINLOCK_TIME_CHECK_INTERVAL - 1U)) ==
                SPINLOCK_TIME_CHECK_INTERVAL - 1U) {
            uint64_t now_cycles = cpu_cycle_counter_read();
            if (now_cycles < start_cycles ||
                now_cycles - start_cycles >= timeout_cycles) return false;
        }
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
    if (!spinlock_acquire_bounded(lock, SPINLOCK_ACQUIRE_TIMED_SPIN_LIMIT)) {
        uint32_t caller =
            (uint32_t)(uintptr_t)__builtin_return_address(0);
        panic_context_set("synchronization", "SMP spinlock", "acquire",
                          "bounded owner wait");
        panic_context_set_result(-110, (uint32_t)(uintptr_t)lock,
            (lock->owner_cpu << 24U) |
                (caller & 0x00FFFFFFU));
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
    uint32_t cpu = x86_cpu_current_index();
    KASSERT(cpu != X86_CPU_INDEX_INVALID);
    KASSERT(lock->lock == cpu + 1U);
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
    uint32_t owner_token = cpu + 1U;
    uint32_t observed = __sync_val_compare_and_swap(
        &lock->lock, 0U, owner_token);
    KASSERT(observed != owner_token);
    if (observed != 0U) return 0;
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
    if (lock == NULL) return false;
    uint32_t cpu = x86_cpu_current_index();
    return cpu != X86_CPU_INDEX_INVALID && lock->lock == cpu + 1U;
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
