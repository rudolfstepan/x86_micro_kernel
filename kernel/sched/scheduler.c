#include "kernel/sched/scheduler.h"

#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/tss.h"
#include "arch/x86/mm/paging.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "include/kernel/panic.h"
#include "kernel/time/pit.h"
#include "include/kernel/watchdog.h"
#include "include/kernel/ipc.h"
#include "mm/kmalloc.h"

extern void swtch(context_t *old, context_t *new);
extern void enter_user_mode(uint32_t entry_point, uint32_t user_stack)
    __attribute__((noreturn));

task_t tasks[MAX_TASKS];
volatile int current_task = -1;
uint8_t num_tasks = 0;

static context_t kernel_context;
static bool kernel_context_saved = false;
static volatile uint32_t preempt_disable_count;
static volatile bool preemption_pending;
static bool apic_timer_active;
static uint32_t pit_scheduler_ticks;
static wait_queue_t sleep_waiters = WAIT_QUEUE_INIT;

#define SCHEDULER_QUANTUM_MS 10U
#define KERNEL_STACK_GUARD 0x4B535447U /* "KSTG" */

extern uint32_t _stack_guard_start;
extern uint32_t _stack_guard_end;
extern uint8_t _stack_start;
extern uint8_t _stack_end;

typedef struct {
    bool allocated;
    uintptr_t frames[STACK_SIZE / PAGE_SIZE];
} kernel_stack_slot_t;

static kernel_stack_slot_t kernel_stack_slots[MAX_TASKS];

static task_t *task_from_wait_node(wait_queue_node_t *node) {
    return (task_t*)((uint8_t*)node - offsetof(task_t, wait_node));
}

static int kernel_stack_slot_for(const uint32_t *stack) {
    uintptr_t address = (uintptr_t)stack;
    if (address < KERNEL_STACK_ARENA_BASE + PAGE_SIZE) return -1;
    uintptr_t offset = address - KERNEL_STACK_ARENA_BASE - PAGE_SIZE;
    if ((offset % KERNEL_STACK_SLOT_SIZE) != 0) return -1;
    size_t slot = offset / KERNEL_STACK_SLOT_SIZE;
    return slot < MAX_TASKS ? (int)slot : -1;
}

uint32_t *scheduler_allocate_kernel_stack(void) {
    KASSERT_NOT_IRQ();
    for (size_t slot = 0; slot < MAX_TASKS; ++slot) {
        if (kernel_stack_slots[slot].allocated) continue;
        uint32_t stack_base = KERNEL_STACK_ARENA_BASE +
                              (uint32_t)slot * KERNEL_STACK_SLOT_SIZE +
                              PAGE_SIZE;
        size_t mapped = 0;
        for (; mapped < STACK_SIZE / PAGE_SIZE; ++mapped) {
            uintptr_t frame = allocate_frame();
            if (frame == 0 ||
                map_page(paging_kernel_directory(),
                         stack_base + (uint32_t)mapped * PAGE_SIZE,
                         (uint32_t)frame, PAGE_RW) != 0) {
                if (frame != 0) free_frame(frame);
                while (mapped != 0) {
                    --mapped;
                    (void)unmap_kernel_page(
                        stack_base + (uint32_t)mapped * PAGE_SIZE, true);
                }
                return NULL;
            }
            kernel_stack_slots[slot].frames[mapped] = frame;
        }
        kernel_stack_slots[slot].allocated = true;
        uint32_t *stack = (uint32_t*)(uintptr_t)stack_base;
        memset(stack, 0, STACK_SIZE);
        return stack;
    }
    return NULL;
}

bool scheduler_kernel_stack_is_valid(const uint32_t *stack) {
    int slot = kernel_stack_slot_for(stack);
    if (slot < 0 || !kernel_stack_slots[slot].allocated) return false;
    uintptr_t base = (uintptr_t)stack;
    if (paging_kernel_page_present((uint32_t)(base - PAGE_SIZE)) ||
        paging_kernel_page_present((uint32_t)(base + STACK_SIZE))) {
        return false;
    }
    for (size_t page = 0; page < STACK_SIZE / PAGE_SIZE; ++page) {
        if (!paging_kernel_page_present((uint32_t)(base + page * PAGE_SIZE))) {
            return false;
        }
    }
    return true;
}

bool scheduler_kernel_context_stack_is_valid(void) {
    if (paging_is_enabled()) {
        return (uintptr_t)&_stack_guard_end -
                   (uintptr_t)&_stack_guard_start == PAGE_SIZE &&
               !paging_kernel_page_present(
                   (uint32_t)(uintptr_t)&_stack_guard_start);
    }
    const uint32_t *guard = &_stack_guard_start;
    const uint32_t *guard_end = &_stack_guard_end;
    if ((uintptr_t)guard_end - (uintptr_t)guard != PAGE_SIZE) return false;
    while (guard < guard_end) {
        if (*guard++ != KERNEL_STACK_GUARD) return false;
    }
    return true;
}

void scheduler_free_kernel_stack(uint32_t *stack) {
    if (stack == NULL) return;
    if (!scheduler_kernel_stack_is_valid(stack)) {
        panic("Kernel stack guard corrupted");
    }
    int slot = kernel_stack_slot_for(stack);
    memset(stack, 0xDD, STACK_SIZE);
    for (size_t page = 0; page < STACK_SIZE / PAGE_SIZE; ++page) {
        if (unmap_kernel_page((uint32_t)(uintptr_t)stack +
                              (uint32_t)page * PAGE_SIZE, true) != 0) {
            panic("Unable to release guarded kernel stack");
        }
        kernel_stack_slots[slot].frames[page] = 0;
    }
    kernel_stack_slots[slot].allocated = false;
}

static void validate_task_stack_or_panic(const task_t *task) {
    if (task == NULL || task->kernel_stack == NULL) return;
    uintptr_t low = (uintptr_t)task->kernel_stack;
    uintptr_t high = low + STACK_SIZE;
    if (!scheduler_kernel_stack_is_valid(task->kernel_stack) ||
        (task->context.esp != 0U &&
         ((uintptr_t)task->context.esp < low ||
          (uintptr_t)task->context.esp >= high))) {
        panic("Kernel stack guard or saved ESP corrupted");
    }
}

static void validate_running_task_stack_or_panic(const task_t *task) {
    validate_task_stack_or_panic(task);
    uintptr_t current_esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
    uintptr_t low = (uintptr_t)task->kernel_stack;
    if (current_esp < low || current_esp >= low + STACK_SIZE) {
        panic("Current ESP escaped the kernel task stack");
    }
}

static void validate_kernel_context_stack_or_panic(bool check_esp) {
    if (!scheduler_kernel_context_stack_is_valid()) {
        panic("Static kernel stack guard corrupted");
    }
    if (check_esp) {
        uintptr_t current_esp;
        __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
        if (current_esp < (uintptr_t)&_stack_start ||
            current_esp >= (uintptr_t)&_stack_end) {
            panic("Current ESP escaped the static kernel stack");
        }
    }
}

static int find_next_runnable(int after) {
    if (num_tasks == 0) {
        return -1;
    }

    for (int step = 1; step <= num_tasks; ++step) {
        int index = (after < 0) ? (step - 1) : ((after + step) % num_tasks);
        if (tasks[index].status == TASK_READY ||
            tasks[index].status == TASK_RUNNING) {
            return index;
        }
    }
    return -1;
}

static void task_trampoline(void) __attribute__((noreturn));
static void task_trampoline(void) {
    int index = current_task;
    if (index < 0 || index >= num_tasks || tasks[index].context.eip == 0) {
        scheduler_kill_current();
    }

    if (tasks[index].user_mode) {
        enter_user_mode(tasks[index].user_entry, tasks[index].user_stack);
    }

    irq_enable();
    void (*entry_point)(void) = (void (*)(void))tasks[index].context.eip;
    entry_point();
    task_exit();
}

static void release_task_resources(task_t *task) {
    int task_id = (int)(task - tasks);
    wait_queue_cancel_locked(task);
    if (task->process != NULL &&
        task->process->generation == task->process_generation &&
        task->process->task_id == task_id) {
        /* A zombie keeps its PID and exit status until wait(), but it must not
         * retain a numerical task slot after that slot becomes reusable. */
        task->process->task_id = -1;
    }
    task->reap_page_directory =
        task->page_directory != paging_kernel_directory()
            ? task->page_directory : NULL;
    task->reap_kernel_stack = task->kernel_stack;
    task->page_directory = NULL;
    task->kernel_stack = NULL;
    task->process = NULL;
    task->process_generation = 0U;
    task->status = TASK_REAPING;
}

int scheduler_reap_finished_task_locked(int task_id, const Process *owner) {
    KASSERT_IRQ_DISABLED();
    if (irq_enabled() || task_id < 0 || task_id >= num_tasks ||
        task_id == current_task || tasks[task_id].status != TASK_FINISHED ||
        (owner != NULL && (tasks[task_id].process != owner ||
                           tasks[task_id].process_generation !=
                               owner->generation))) {
        return -1;
    }
    release_task_resources(&tasks[task_id]);
    return 0;
}

size_t scheduler_reap_finished_tasks(void) {
    size_t reaped = 0;
    scheduler_preempt_disable();

    uint32_t flags = irq_save();
    for (int task_id = 0; task_id < num_tasks; ++task_id) {
        task_t *task = &tasks[task_id];
        bool owns_resources = task->kernel_stack != NULL ||
                              task->page_directory != NULL ||
                              task->process != NULL ||
                              task->wait_node.queue != NULL;
        if (task_id != current_task && task->status == TASK_FINISHED &&
            owns_resources) {
            release_task_resources(task);
        }
    }
    irq_restore(flags);

    /* Page-directory walks and heap coalescing can be proportional to a
     * process's allocation count.  Detach atomically above, then do that work
     * with hardware interrupts enabled while task preemption is suppressed. */
    for (int task_id = 0; task_id < num_tasks; ++task_id) {
        page_directory_t *page_directory = NULL;
        uint32_t *kernel_stack = NULL;

        flags = irq_save();
        task_t *task = &tasks[task_id];
        if (task->status == TASK_REAPING &&
            (task->reap_page_directory != NULL ||
             task->reap_kernel_stack != NULL)) {
            page_directory = task->reap_page_directory;
            kernel_stack = task->reap_kernel_stack;
            task->reap_page_directory = NULL;
            task->reap_kernel_stack = NULL;
        }
        irq_restore(flags);

        if (page_directory != NULL) free_page_directory(page_directory);
        if (kernel_stack != NULL) scheduler_free_kernel_stack(kernel_stack);
        if (page_directory == NULL && kernel_stack == NULL) continue;

        flags = irq_save();
        task = &tasks[task_id];
        if (task->status == TASK_REAPING &&
            task->reap_page_directory == NULL &&
            task->reap_kernel_stack == NULL) {
            memset(task, 0, sizeof(*task));
            task->status = TASK_FINISHED;
            ++reaped;
        }
        irq_restore(flags);
    }

    scheduler_preempt_enable();
    return reaped;
}

int create_task(void (*entry_point)(void), uint32_t *stack, Process *process) {
    if (!entry_point || !stack || !scheduler_kernel_stack_is_valid(stack)) {
        return -1;
    }

    uint32_t flags = irq_save();
    int task_id = -1;
    for (int i = 0; i < num_tasks; ++i) {
        if (tasks[i].status == TASK_FINISHED && i != current_task &&
            tasks[i].kernel_stack == NULL &&
            tasks[i].reap_kernel_stack == NULL &&
            tasks[i].reap_page_directory == NULL) {
            task_id = i;
            break;
        }
    }

    if (task_id < 0) {
        if (num_tasks >= MAX_TASKS) {
            irq_restore(flags);
            printf("Error: Maximum number of tasks reached!\n");
            return -1;
        }
        task_id = num_tasks;
    }

    task_t *task = &tasks[task_id];
    memset(task, 0, sizeof(*task));
    task->status = TASK_FINISHED;
    task->kernel_stack = stack;
    task->context.eip = (uint32_t)entry_point;
    task->is_started = 1;
    task->process = process;
    task->process_generation = process != NULL ? process->generation : 0U;
    task->page_directory = paging_kernel_directory();

    uintptr_t top = ((uintptr_t)stack + STACK_SIZE) & ~(uintptr_t)0x0F;
    uint32_t *initial_stack = (uint32_t*)top;
    *(--initial_stack) = (uint32_t)task_exit;       // Fallback return address
    *(--initial_stack) = (uint32_t)task_trampoline; // First RET target
    task->context.esp = (uint32_t)initial_stack;
    if (process != NULL) {
        process->task_id = task_id;
    }
    task->status = TASK_READY;
    if (task_id == num_tasks) {
        num_tasks++;
    }

    irq_restore(flags);
    return task_id;
}

static size_t available_task_slots_locked(void) {
    size_t available = MAX_TASKS - num_tasks;
    for (int index = 0; index < num_tasks; ++index) {
        if (index != current_task && tasks[index].status == TASK_FINISHED &&
            tasks[index].kernel_stack == NULL &&
            tasks[index].reap_kernel_stack == NULL &&
            tasks[index].reap_page_directory == NULL) ++available;
    }
    return available;
}

static int create_user_task_admitted(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process, bool supervised) {
    if (entry_point < USER_BASE || entry_point >= USER_TOP ||
        user_stack <= USER_BASE || user_stack > USER_TOP || !kernel_stack ||
        !page_directory) return -1;
    scheduler_preempt_disable();
    uint32_t admission_flags = irq_save();
    size_t available = available_task_slots_locked();
    bool admitted = available != 0U &&
        (supervised || available > SUPERVISED_TASK_RESERVE);
    irq_restore(admission_flags);
    if (!admitted) {
        scheduler_preempt_enable();
        return -1;
    }
    int task_id = create_task((void (*)(void))(uintptr_t)entry_point,
                              kernel_stack, process);
    if (task_id < 0) {
        scheduler_preempt_enable();
        return -1;
    }
    uint32_t flags = irq_save();
    task_t *task = &tasks[task_id];
    task->page_directory = page_directory;
    task->user_entry = entry_point;
    task->user_stack = user_stack;
    task->user_mode = true;
    irq_restore(flags);
    scheduler_preempt_enable();
    return task_id;
}

int create_user_task(uint32_t entry_point, uint32_t user_stack,
                     uint32_t *kernel_stack, page_directory_t *page_directory,
                     Process *process) {
    return create_user_task_admitted(entry_point, user_stack, kernel_stack,
                                     page_directory, process, false);
}

int create_supervised_user_task(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process) {
    return create_user_task_admitted(entry_point, user_stack, kernel_stack,
                                     page_directory, process, true);
}

static void activate_task_address_space(int task_index) {
    if (task_index >= 0 && task_index < num_tasks) {
        task_t *task = &tasks[task_index];
        validate_task_stack_or_panic(task);
        switch_page_directory(task->page_directory);
        tss_set_kernel_stack((uint32_t)(uintptr_t)task->kernel_stack +
                             STACK_SIZE);
    } else {
        switch_page_directory(paging_kernel_directory());
    }
}

static bool schedule_blocked_current_locked(int blocked) {
    validate_running_task_stack_or_panic(&tasks[blocked]);
    int next = find_next_runnable(blocked);
    if (next >= 0) {
        current_task = next;
        tasks[next].status = TASK_RUNNING;
        activate_task_address_space(next);
        swtch(&tasks[blocked].context, &tasks[next].context);
        return true;
    }

    if (!kernel_context_saved) return false;
    validate_kernel_context_stack_or_panic(false);
    current_task = -1;
    activate_task_address_space(-1);
    swtch(&tasks[blocked].context, &kernel_context);
    return true;
}

void wait_queue_cancel_locked(task_t *task) {
    KASSERT_IRQ_DISABLED();
    if (task == NULL || task->wait_node.queue == NULL) return;
    (void)wait_queue_remove_locked(task->wait_node.queue, &task->wait_node);
    task->wait_node.key = 0;
    task->wait_deadline_ms = 0;
}

int wait_queue_block_until_locked(wait_queue_t *queue,
                                  task_block_kind_t kind,
                                  uint64_t deadline_ms) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    if (queue == NULL || irq_enabled() || preempt_disable_count != 0 ||
        current_task < 0 || current_task >= num_tasks ||
        (kind != TASK_BLOCK_WAITING && kind != TASK_BLOCK_SLEEPING)) {
        return -1;
    }

    int blocked = current_task;
    task_t *task = &tasks[blocked];
    if (task->status != TASK_RUNNING || task->wait_node.queue != NULL ||
        !wait_queue_push_locked(queue, &task->wait_node)) {
        return -1;
    }
    task->wait_deadline_ms = deadline_ms;
    task->wait_result = 0;
    task->status = kind == TASK_BLOCK_SLEEPING
        ? TASK_SLEEPING : TASK_WAITING;

    if (!schedule_blocked_current_locked(blocked)) {
        wait_queue_cancel_locked(task);
        task->status = TASK_RUNNING;
        current_task = blocked;
        return -1;
    }
    return task->wait_result;
}

int wait_queue_block_locked(wait_queue_t *queue, task_block_kind_t kind) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    return wait_queue_block_until_locked(queue, kind, UINT64_MAX);
}

bool wait_queue_wake_one_locked(wait_queue_t *queue) {
    KASSERT_IRQ_DISABLED();
    if (queue == NULL || irq_enabled()) return false;
    for (;;) {
        wait_queue_node_t *node = wait_queue_pop_locked(queue);
        if (node == NULL) return false;
        task_t *task = task_from_wait_node(node);
        node->key = 0;
        if (task->status == TASK_WAITING || task->status == TASK_SLEEPING) {
            task->wait_deadline_ms = 0;
            task->wait_result = 0;
            task->status = TASK_READY;
            return true;
        }
    }
}

void scheduler_wake_expired_waiters_locked(uint64_t now_ms) {
    KASSERT_IRQ_DISABLED();
    if (irq_enabled()) return;
    for (size_t index = 0; index < MAX_TASKS; ++index) {
        task_t *task = &tasks[index];
        if (task->status != TASK_WAITING || task->wait_node.queue == NULL ||
            task->wait_deadline_ms == UINT64_MAX ||
            task->wait_deadline_ms > now_ms) continue;
        if (wait_queue_remove_locked(task->wait_node.queue,
                                     &task->wait_node)) {
            task->wait_deadline_ms = 0;
            task->wait_result = -110;
            task->status = TASK_READY;
        }
    }
}

size_t wait_queue_wake_all_locked(wait_queue_t *queue) {
    KASSERT_IRQ_DISABLED();
    size_t count = 0;
    while (wait_queue_wake_one_locked(queue)) ++count;
    return count;
}

void scheduler_wake_expired_sleepers_locked(uint64_t now_ms) {
    KASSERT_IRQ_DISABLED();
    if (irq_enabled()) return;
    while (sleep_waiters.head != NULL &&
           sleep_waiters.head->key <= now_ms) {
        (void)wait_queue_wake_one_locked(&sleep_waiters);
    }
}

int scheduler_sleep_ms(uint32_t milliseconds) {
    KASSERT_CAN_SLEEP();
    if (milliseconds == 0) return 0;
    uint32_t flags = irq_save();
    if (preempt_disable_count != 0 || irq_in_context() || current_task < 0 ||
        current_task >= num_tasks) {
        irq_restore(flags);
        return -1;
    }

    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)milliseconds;
    if (deadline < now) deadline = UINT64_MAX;

    int blocked = current_task;
    task_t *task = &tasks[blocked];
    if (task->status != TASK_RUNNING || task->wait_node.queue != NULL ||
        !wait_queue_insert_ordered_locked(&sleep_waiters, &task->wait_node,
                                          deadline)) {
        irq_restore(flags);
        return -1;
    }
    task->status = TASK_SLEEPING;
    if (!schedule_blocked_current_locked(blocked)) {
        wait_queue_cancel_locked(task);
        task->status = TASK_RUNNING;
        current_task = blocked;
        irq_restore(flags);
        return -1;
    }
    irq_restore(flags);
    return 0;
}

int scheduler_yield(void) {
    KASSERT_CAN_SLEEP();
    uint32_t flags = irq_save();
    if (preempt_disable_count != 0) {
        irq_restore(flags);
        return -1;
    }
    int previous = current_task;
    if (previous < 0 || previous >= num_tasks) {
        irq_restore(flags);
        return 0;
    }
    validate_running_task_stack_or_panic(&tasks[previous]);

    tasks[previous].status = TASK_READY;
    int next = find_next_runnable(previous);
    if (next < 0 || next == previous) {
        tasks[previous].status = TASK_RUNNING;
        irq_restore(flags);
        return 0;
    }

    current_task = next;
    tasks[next].status = TASK_RUNNING;
    activate_task_address_space(next);
    swtch(&tasks[previous].context, &tasks[next].context);
    irq_restore(flags);
    return 0;
}

void scheduler_set_apic_timer_active(bool active) {
    uint32_t flags = irq_save();
    apic_timer_active = active;
    pit_scheduler_ticks = 0;
    irq_restore(flags);
}

bool scheduler_uses_pit_fallback(void) {
    uint32_t flags = irq_save();
    bool fallback = !apic_timer_active;
    irq_restore(flags);
    return fallback;
}

void scheduler_pit_interrupt_handler(void) {
    if (!scheduler_uses_pit_fallback()) return;
    if (++pit_scheduler_ticks < SCHEDULER_QUANTUM_MS) return;
    pit_scheduler_ticks = 0;
    scheduler_interrupt_handler();
}

void scheduler_interrupt_handler(void) {
    uint32_t flags = irq_save();
    if (preempt_disable_count != 0) {
        preemption_pending = true;
        irq_restore(flags);
        return;
    }
    preemption_pending = false;
    int previous = current_task;
    if (previous >= 0 && previous < num_tasks) {
        validate_running_task_stack_or_panic(&tasks[previous]);
    } else {
        validate_kernel_context_stack_or_panic(true);
    }
    /* Feeding eligibility is earned only after the scheduler reached its
     * validation point with preemption enabled. Merely receiving IRQ0 does
     * not constitute system progress. */
    watchdog_health_progress();
    int next = find_next_runnable(previous);

    if (next < 0 || next == previous) {
        irq_restore(flags);
        return;
    }

    current_task = next;
    tasks[next].status = TASK_RUNNING;
    activate_task_address_space(next);

    if (previous < 0) {
        kernel_context_saved = true;
        swtch(&kernel_context, &tasks[next].context);
    } else {
        if (tasks[previous].status == TASK_RUNNING) {
            tasks[previous].status = TASK_READY;
        }
        swtch(&tasks[previous].context, &tasks[next].context);
    }

    irq_restore(flags);
}

void scheduler_preempt_disable(void) {
    uint32_t flags = irq_save();
    KASSERT(preempt_disable_count < UINT32_MAX);
    ++preempt_disable_count;
    irq_restore(flags);
}

void scheduler_preempt_enable(void) {
    uint32_t flags = irq_save();
    KASSERT(preempt_disable_count != 0);
    --preempt_disable_count;
    /* Do not context-switch from inside the unlock path.  The periodic timer
     * will observe preempt_disable_count == 0 on its next tick, clear the
     * pending marker and schedule normally from interrupt context. */
    irq_restore(flags);
}

bool scheduler_preempt_is_disabled(void) {
    uint32_t flags = irq_save();
    bool disabled = preempt_disable_count != 0;
    irq_restore(flags);
    return disabled;
}

bool scheduler_can_sleep(void) {
    return irq_enabled() && !irq_in_context() &&
           !scheduler_preempt_is_disabled();
}

void scheduler_terminate_task(int task_id) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    KASSERT(scheduler_preempt_is_disabled());
    if (task_id < 0 || task_id >= num_tasks) {
        return;
    }
    KASSERT(task_id != current_task);

    task_t *task = &tasks[task_id];
    Process *process = task->process;
    uint32_t generation = task->process_generation;
    if (task->status == TASK_FINISHED || task->status == TASK_REAPING ||
        process == NULL || process->generation != generation) {
        return;
    }

    /* VFS teardown may reach block drivers and must run with IF=1, outside the
     * scheduler's IRQ-disabled commit.  The caller's preemption guard keeps
     * the target slot and generation stable on this UP scheduler. */
    ipc_process_cleanup(process->pid, generation);
    process_close_all_files(process);
    process_orphan_children(process->pid);

    uint32_t flags = irq_save();
    KASSERT(task->process == process &&
            task->process_generation == generation);
    wait_queue_cancel_locked(task);
    process->exit_status = 143;
    process->has_exited = true;
    process->is_running = false;
    (void)wait_queue_wake_all_locked(&process->exit_waiters);
    task->status = TASK_FINISHED;
    irq_restore(flags);
}

void task_exit(void) {
    task_exit_status(0);
}

void task_exit_status(int status) {
    KASSERT_NOT_IRQ();
    KASSERT(!scheduler_preempt_is_disabled());
    scheduler_preempt_disable();

    int exiting = current_task;
    Process *process = NULL;
    uint32_t process_generation = 0U;
    if (exiting >= 0 && exiting < num_tasks) {
        validate_running_task_stack_or_panic(&tasks[exiting]);
        process = tasks[exiting].process;
        process_generation = tasks[exiting].process_generation;
    }

    /* User exceptions arrive with IF=0.  Keep scheduling suppressed while
     * temporarily enabling device IRQs for VFS/block-driver cleanup. */
    irq_enable();
    if (process != NULL) {
        ipc_process_cleanup(process->pid, process_generation);
        process_close_all_files(process);
        process_orphan_children(process->pid);
    }
    irq_disable();
    scheduler_preempt_enable();

    int finished = current_task;
    if (finished >= 0 && finished < num_tasks) {
        wait_queue_cancel_locked(&tasks[finished]);
        tasks[finished].status = TASK_FINISHED;
        if (tasks[finished].process) {
            tasks[finished].process->exit_status = status;
            tasks[finished].process->has_exited = true;
            tasks[finished].process->is_running = false;
            (void)wait_queue_wake_all_locked(
                &tasks[finished].process->exit_waiters);
        }
    }

    int next = find_next_runnable(finished);
    if (next >= 0) {
        current_task = next;
        tasks[next].status = TASK_RUNNING;
        activate_task_address_space(next);
        swtch(NULL, &tasks[next].context);
    }

    current_task = -1;
    if (kernel_context_saved) {
        validate_kernel_context_stack_or_panic(false);
        activate_task_address_space(-1);
        swtch(NULL, &kernel_context);
    }

    cpu_halt_forever();
    __builtin_unreachable();
}

void scheduler_kill_current(void) {
    task_exit();
}

Process *scheduler_current_process(void) {
    int index = current_task;
    if (index < 0 || index >= num_tasks || !tasks[index].user_mode) {
        return NULL;
    }
    return tasks[index].process;
}

void list_tasks(void) {
    printf("Task list:\n");
    for (int i = 0; i < num_tasks; ++i) {
        const char *status = "Ready";
        if (tasks[i].status == TASK_RUNNING) status = "Running";
        else if (tasks[i].status == TASK_SLEEPING) status = "Sleeping";
        else if (tasks[i].status == TASK_WAITING) status = "Waiting";
        else if (tasks[i].status == TASK_FINISHED) status = "Finished";

        printf("Task %d: EIP=%p, ESP=%p, Status=%s\n",
               i, (void*)(uintptr_t)tasks[i].context.eip,
               (void*)(uintptr_t)tasks[i].context.esp, status);
    }
}
