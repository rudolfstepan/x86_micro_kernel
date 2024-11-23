#include "scheduler.h"
#include "process.h"
#include "toolchain/stdio.h"


// Task-Liste
volatile task_t tasks[MAX_TASKS] = {0};
volatile uint8_t current_task = 0; // ID des aktuellen Tasks
uint8_t num_tasks = 0;    // Anzahl der registrierten Tasks

// Save and restore task context
void scheduler_save_context(task_t *task) {
    // Save registers and stack pointer

    // save the stack pointer
    __asm__ __volatile__("mov %%esp, %0" : "=r"(task->stack_pointer));
}

void scheduler_restore_context(task_t *task) {
    // Restore registers and stack pointer

    // restore the stack pointer
    __asm__ __volatile__("mov %0, %%esp" : : "r"(task->stack_pointer));
}

// Scheduler
void scheduler_interrupt_handler(void* r) {
    // Save the current task's state (registers, stack pointer, etc.)
    if (tasks[current_task].status == TASK_RUNNING) {
        tasks[current_task].status = TASK_READY;
        // Save the current task's context
        scheduler_save_context(&tasks[current_task]);
    }

    // Select the next task (e.g., round-robin scheduling)
    current_task = (current_task + 1) % num_tasks;

    // Resume the next task
    if (tasks[current_task].status != TASK_RUNNING) {
        tasks[current_task].status = TASK_RUNNING;
        start_program_execution(tasks[current_task].entry_point);
    } else {
        // Restore the next task's context
        scheduler_restore_context(&tasks[current_task]);
    }

    // Return from the interrupt
}

// create a new task
void create_task(void (*entry_point)(void), uint32_t* stack, size_t stack_size) {
    uint32_t* stack_top = stack + stack_size / sizeof(uint32_t);

    // Push a "fake" return address (this will return to `schedule()`)
    *(--stack_top) = (uint32_t)scheduler_interrupt_handler;

    // Set up the initial stack frame for the task
    *(--stack_top) = 0;             // EBP
    *(--stack_top) = 0;             // EDI
    *(--stack_top) = 0;             // ESI
    *(--stack_top) = 0;             // EDX
    *(--stack_top) = 0;             // ECX
    *(--stack_top) = 0;             // EBX
    *(--stack_top) = 0;             // EAX

    tasks[num_tasks].stack_pointer = stack_top;
    tasks[num_tasks].entry_point = entry_point;
    tasks[num_tasks].status = TASK_READY;

    printf("Task %d created with stack pointer: %p\n", num_tasks, stack_top);
    num_tasks++;
}
