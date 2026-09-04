/**
 * @file kernel/sched/mutex.c
 * @brief Deadline-bounded recursive mutex built on the intrusive wait queue.
 */
#include "kernel/sched/mutex.h"

#include "arch/x86/include/cpu_local.h"
#include "include/kernel/panic.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"

#include <stdbool.h>

typedef struct {
    int task;
    uint32_t generation;
    uint32_t cpu;
} kernel_mutex_identity_t;

static bool kernel_mutex_owner_identity(kernel_mutex_identity_t *identity) {
    if (identity == NULL) return false;
    uint32_t cpu = x86_cpu_current_index();
    if (cpu == X86_CPU_INDEX_INVALID || cpu >= X86_CPU_LOCAL_MAX) return false;

    int task = -1;
    uint32_t generation = 0U;
    (void)scheduler_current_task_identity(&task, &generation);
    if (task >= MAX_TASKS || (task >= 0 && generation == 0U)) return false;
    identity->task = task;
    identity->generation = generation;
    identity->cpu = cpu;
    return true;
}

static bool kernel_mutex_identity_matches(const kernel_mutex_t *mutex,
                                          const kernel_mutex_identity_t *id) {
    if (mutex->owner_task >= 0)
        return mutex->owner_task == id->task &&
               mutex->owner_generation == id->generation;
    return mutex->owner_task == -1 && id->task == -1 &&
           mutex->owner_cpu == id->cpu;
}

void kernel_mutex_init(kernel_mutex_t *mutex) {
    KASSERT(mutex != NULL);
    spinlock_init(&mutex->state_lock);
    wait_queue_init(&mutex->waiters);
    mutex->owner_task = KERNEL_MUTEX_NO_OWNER_TASK;
    mutex->owner_generation = 0U;
    mutex->owner_cpu = X86_CPU_INDEX_INVALID;
    mutex->recursion_depth = 0U;
}

int kernel_mutex_lock_until(kernel_mutex_t *mutex, uint64_t deadline_ms) {
    KASSERT_NOT_IRQ();
    if (mutex == NULL) return KERNEL_MUTEX_INVALID;

    kernel_mutex_identity_t identity;
    if (!kernel_mutex_owner_identity(&identity))
        return KERNEL_MUTEX_INVALID;

    /* A task that currently cannot sleep may still take an uncontended or
     * recursive mutex.  Snapshot that permission before irq_save(): actual
     * contention must fail bounded instead of entering a wait queue. */
    bool may_block = scheduler_can_sleep();

    /* A per-CPU kernel context is not a runnable scheduler task. Keep it on
     * the CPU while it owns a mutex, otherwise a timer switch can strand an
     * owner_task=-1 lock indefinitely. Kernel contexts never wait. */
    bool kernel_preempt_guard = false;
    if (identity.task < 0) {
        /* One guard per successful recursive acquisition keeps nesting with
         * other kernel mutexes balanced regardless of unlock order. */
        scheduler_preempt_disable();
        kernel_preempt_guard = true;
    }

    for (;;) {
        uint32_t flags = irq_save();
        spinlock_acquire(&mutex->state_lock);
        if (mutex->owner_task == KERNEL_MUTEX_NO_OWNER_TASK) {
            KASSERT(mutex->recursion_depth == 0U);
            mutex->owner_task = identity.task;
            mutex->owner_generation = identity.generation;
            mutex->owner_cpu = identity.cpu;
            mutex->recursion_depth = 1U;
            if (identity.task >= 0 && scheduler_mutex_owner_register(
                    identity.task, identity.generation, mutex) != 0) {
                mutex->owner_task = KERNEL_MUTEX_NO_OWNER_TASK;
                mutex->owner_generation = 0U;
                mutex->owner_cpu = X86_CPU_INDEX_INVALID;
                mutex->recursion_depth = 0U;
                (void)wait_queue_wake_one_locked(&mutex->waiters);
                spinlock_release_irq(&mutex->state_lock, flags);
                return KERNEL_MUTEX_INVALID;
            }
            spinlock_release_irq(&mutex->state_lock, flags);
            return 0;
        }
        if (kernel_mutex_identity_matches(mutex, &identity)) {
            if (mutex->recursion_depth >= KERNEL_MUTEX_RECURSION_LIMIT) {
                spinlock_release_irq(&mutex->state_lock, flags);
                if (kernel_preempt_guard) scheduler_preempt_enable();
                return KERNEL_MUTEX_INVALID;
            }
            ++mutex->recursion_depth;
            spinlock_release_irq(&mutex->state_lock, flags);
            return 0;
        }

        uint64_t now = pit_monotonic_ms();
        if (identity.task < 0 || !may_block) {
            spinlock_release_irq(&mutex->state_lock, flags);
            if (kernel_preempt_guard) scheduler_preempt_enable();
            return KERNEL_MUTEX_WOULD_BLOCK;
        }
        if (now >= deadline_ms) {
            spinlock_release_irq(&mutex->state_lock, flags);
            if (kernel_preempt_guard) scheduler_preempt_enable();
            return KERNEL_MUTEX_WAIT_TIMEOUT;
        }

        int result = wait_queue_block_until_spinlocked(
            &mutex->waiters, TASK_BLOCK_WAITING, deadline_ms,
            &mutex->state_lock, flags);
        if (result != 0) return result;
        /* Wakeups transfer no ownership. Recheck under state_lock so timeout,
         * cancellation and competing waiters all share one state transition. */
    }
}

int kernel_mutex_lock_for(kernel_mutex_t *mutex, uint32_t timeout_ms) {
    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)timeout_ms;
    if (deadline < now) deadline = UINT64_MAX;
    return kernel_mutex_lock_until(mutex, deadline);
}

void kernel_mutex_unlock(kernel_mutex_t *mutex) {
    KASSERT_NOT_IRQ();
    KASSERT(mutex != NULL);

    kernel_mutex_identity_t identity;
    KASSERT(kernel_mutex_owner_identity(&identity));

    uint32_t flags = irq_save();
    spinlock_acquire(&mutex->state_lock);
    KASSERT(kernel_mutex_identity_matches(mutex, &identity));
    KASSERT(mutex->recursion_depth > 0U);
    bool release_kernel_preempt_guard = mutex->owner_task == -1;
    --mutex->recursion_depth;
    if (mutex->recursion_depth == 0U) {
        if (mutex->owner_task >= 0)
            KASSERT(scheduler_mutex_owner_unregister(
                mutex->owner_task, mutex->owner_generation, mutex) == 0);
        mutex->owner_task = KERNEL_MUTEX_NO_OWNER_TASK;
        mutex->owner_generation = 0U;
        mutex->owner_cpu = X86_CPU_INDEX_INVALID;
        (void)wait_queue_wake_one_locked(&mutex->waiters);
    }
    spinlock_release_irq(&mutex->state_lock, flags);
    if (release_kernel_preempt_guard) scheduler_preempt_enable();
}

bool kernel_mutex_abandon_task_owner(kernel_mutex_t *mutex, int task_id,
                                     uint32_t task_generation) {
    KASSERT_NOT_IRQ();
    if (mutex == NULL || task_id < 0 || task_generation == 0U) return false;
    uint32_t flags = irq_save();
    spinlock_acquire(&mutex->state_lock);
    bool matched = mutex->owner_task == task_id &&
                   mutex->owner_generation == task_generation &&
                   mutex->recursion_depth != 0U;
    if (matched) {
        mutex->owner_task = KERNEL_MUTEX_NO_OWNER_TASK;
        mutex->owner_generation = 0U;
        mutex->owner_cpu = X86_CPU_INDEX_INVALID;
        mutex->recursion_depth = 0U;
        (void)wait_queue_wake_one_locked(&mutex->waiters);
    }
    spinlock_release_irq(&mutex->state_lock, flags);
    return matched;
}
