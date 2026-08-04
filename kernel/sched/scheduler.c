#include "kernel/sched/scheduler.h"

#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/tss.h"
#include "arch/x86/mm/paging.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
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
    if (task->page_directory &&
        task->page_directory != paging_kernel_directory()) {
        free_page_directory(task->page_directory);
    }
    if (task->kernel_stack) k_free(task->kernel_stack);
    memset(task, 0, sizeof(*task));
    task->status = TASK_FINISHED;
}

int create_task(void (*entry_point)(void), uint32_t *stack, Process *process) {
    if (!entry_point || !stack) {
        return -1;
    }

    uint32_t flags = irq_save();
    int task_id = -1;
    for (int i = 0; i < num_tasks; ++i) {
        if (tasks[i].status == TASK_FINISHED && i != current_task) {
            release_task_resources(&tasks[i]);
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

int create_user_task(uint32_t entry_point, uint32_t user_stack,
                     uint32_t *kernel_stack, page_directory_t *page_directory,
                     Process *process) {
    if (entry_point < USER_BASE || entry_point >= USER_TOP ||
        user_stack <= USER_BASE || user_stack > USER_TOP || !kernel_stack ||
        !page_directory) return -1;
    scheduler_preempt_disable();
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

static void activate_task_address_space(int task_index) {
    if (task_index >= 0 && task_index < num_tasks) {
        task_t *task = &tasks[task_index];
        switch_page_directory(task->page_directory);
        tss_set_kernel_stack((uint32_t)(uintptr_t)task->kernel_stack +
                             STACK_SIZE);
    } else {
        switch_page_directory(paging_kernel_directory());
    }
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
    if (preempt_disable_count != UINT32_MAX) {
        ++preempt_disable_count;
    }
    irq_restore(flags);
}

void scheduler_preempt_enable(void) {
    uint32_t flags = irq_save();
    if (preempt_disable_count == 0) {
        irq_restore(flags);
        return;
    }
    --preempt_disable_count;
    /* Do not context-switch from inside the unlock path.  The periodic timer
     * will observe preempt_disable_count == 0 on its next tick, clear the
     * pending marker and schedule normally from interrupt context. */
    irq_restore(flags);
}

void scheduler_terminate_task(int task_id) {
    uint32_t flags = irq_save();
    if (task_id < 0 || task_id >= num_tasks) {
        irq_restore(flags);
        return;
    }

    if (task_id == current_task) {
        task_exit();
    }

    if (tasks[task_id].process) {
        process_close_all_files(tasks[task_id].process);
        process_orphan_children(tasks[task_id].process->pid);
        tasks[task_id].process->exit_status = 143;
        tasks[task_id].process->has_exited = true;
        tasks[task_id].process->is_running = false;
    }
    tasks[task_id].status = TASK_FINISHED;
    irq_restore(flags);
}

void task_exit(void) {
    task_exit_status(0);
}

void task_exit_status(int status) {
    irq_disable();

    int exiting = current_task;
    if (exiting >= 0 && exiting < num_tasks && tasks[exiting].process) {
        process_close_all_files(tasks[exiting].process);
    }

    int finished = current_task;
    if (finished >= 0 && finished < num_tasks) {
        tasks[finished].status = TASK_FINISHED;
        if (tasks[finished].process) {
            process_orphan_children(tasks[finished].process->pid);
            tasks[finished].process->exit_status = status;
            tasks[finished].process->has_exited = true;
            tasks[finished].process->is_running = false;
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
        else if (tasks[i].status == TASK_FINISHED) status = "Finished";

        printf("Task %d: EIP=%p, ESP=%p, Status=%s\n",
               i, (void*)(uintptr_t)tasks[i].context.eip,
               (void*)(uintptr_t)tasks[i].context.esp, status);
    }
}
