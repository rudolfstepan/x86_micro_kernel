/**
 * @file kernel/sched/mutex.h
 * @brief Deadline-bounded sleepable kernel mutex.
 *
 * Layer: Ring-0 scheduler.
 * Contract: Recursive ownership is task-scoped; contention blocks only a
 * foreground task and every wait has a monotonic deadline. A per-CPU kernel
 * context never waits and remains non-preemptible while it owns the mutex.
 * Safety: The internal spinlock is never held across a context switch.
 */
#ifndef KERNEL_SCHED_MUTEX_H
#define KERNEL_SCHED_MUTEX_H

#include <stdint.h>

#include "include/lib/spinlock.h"
#include "kernel/sched/wait_queue.h"

#define KERNEL_MUTEX_WAIT_TIMEOUT (-110)
#define KERNEL_MUTEX_WOULD_BLOCK (-11)
#define KERNEL_MUTEX_INVALID (-22)
#define KERNEL_MUTEX_RECURSION_LIMIT 1024U
#define KERNEL_MUTEX_NO_OWNER_TASK (-2)

typedef struct {
    spinlock_t state_lock;
    wait_queue_t waiters;
    int32_t owner_task;
    uint32_t owner_generation;
    uint32_t owner_cpu;
    uint32_t recursion_depth;
    uint32_t owner_preempt_pinned;
} kernel_mutex_t;

#define KERNEL_MUTEX_INIT { \
    .state_lock = SPINLOCK_INIT, \
    .waiters = WAIT_QUEUE_INIT, \
    .owner_task = KERNEL_MUTEX_NO_OWNER_TASK, \
    .owner_generation = 0U, \
    .owner_cpu = X86_CPU_INDEX_INVALID, \
    .recursion_depth = 0U, \
    .owner_preempt_pinned = 0U \
}

void kernel_mutex_init(kernel_mutex_t *mutex);

/** Acquire before the absolute monotonic deadline in milliseconds. */
int kernel_mutex_lock_until(kernel_mutex_t *mutex, uint64_t deadline_ms);

/** Acquire within timeout_ms; timeout zero is a non-blocking attempt. */
int kernel_mutex_lock_for(kernel_mutex_t *mutex, uint32_t timeout_ms);

/** Nonblocking acquisition for a current running task whose caller already
 * disabled preemption. Success retains an internal pin through final release. */
int kernel_mutex_trylock_pinned(kernel_mutex_t *mutex);

/** Release one recursive level.  A non-owner release is a kernel defect. */
void kernel_mutex_unlock(kernel_mutex_t *mutex);

/** Revoke one exact non-running task generation's ownership. The scheduler
 * must first detach the mutex from that pinned task's fixed ownership set. */
bool kernel_mutex_abandon_task_owner(kernel_mutex_t *mutex, int task_id,
                                     uint32_t task_generation);

#endif
