#include "scheduler.h"
#include "process.h"
#include "toolchain/stdio.h"


// Task-Liste
volatile task_t tasks[MAX_TASKS] = {0};
volatile uint8_t current_task = 0; // ID des aktuellen Tasks
uint8_t num_tasks = 0;    // Anzahl der registrierten Tasks

// create a new task
void create_task(void (*entry_point)(void), uint32_t* stack, size_t stack_size) {
    uint32_t* stack_top = stack + stack_size / sizeof(uint32_t);

    // Push a "fake" return address (this will return to `schedule()`)
    *(--stack_top) = (uint32_t)schedule;

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

extern void context_switch(uint32_t** old_sp, uint32_t* new_sp);

void context_switch_debug(uint32_t** old_sp, uint32_t* new_sp) {
    printf("Switching context:\n");

    if (old_sp) {
        printf("Saving old stack pointer: %p\n", *old_sp);
    } else {
        printf("Old stack pointer is NULL, skipping save.\n");
    }

    if (new_sp) {
        printf("Loading new stack pointer: %p\n", new_sp);
    } else {
        printf("New stack pointer is NULL, skipping load.\n");
    }

    context_switch(old_sp, new_sp);

    printf("Context switch complete.\n");
}

// Hilfsfunktion für den Scheduler
void schedule() {
    printf("Scheduler called\n");

    if(num_tasks == 0) {
        printf("No tasks to run\n");
        return;
    }

    // Save the current task's state
    if (tasks[current_task].status == TASK_RUNNING) {
        tasks[current_task].status = TASK_READY;

        // Save the current stack pointer
        context_switch_debug(&tasks[current_task].stack_pointer, NULL);
    }

    // Select the next task in a round-robin fashion
    current_task = (current_task + 1) % num_tasks;

    // Start or resume the next task
    if (tasks[current_task].status != TASK_RUNNING) {
        tasks[current_task].status = TASK_RUNNING;

        // Start the task for the first time
        start_program_execution(tasks[current_task].entry_point);

        printf("Task %d started\n", current_task);
    } else {
        // Resume the task
        context_switch_debug(NULL, tasks[current_task].stack_pointer);
    }

    // This point is never reached during normal operation because the scheduler
    // transfers control to the task or another context.
}
