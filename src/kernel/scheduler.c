#include "scheduler.h"
#include "process.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

//extern void swtch(context_t *old, context_t *new);

// Task-Liste
task_t tasks[MAX_TASKS];
volatile uint8_t current_task = 0; // ID des aktuellen Tasks
uint8_t num_tasks = 0;    // Anzahl der registrierten Tasks

// Scheduler
void scheduler_interrupt_handler() {
    asm volatile("cli");
    // Save the current task's context
    //printf("Scheduler interrupt\n");
    task_t *current = &tasks[current_task];
    task_t *next;

    // Select the next task to run
    current_task = (current_task + 1) % num_tasks;
    next = &tasks[current_task];

    if(tasks[current_task].status != TASK_RUNNING){
        tasks[current_task].status = TASK_RUNNING;

        if(!tasks[current_task].is_started){
            tasks[current_task].is_started = 1;
            start_task(current_task);
        }
    }else{
        printf("Switching from task %d to task %d\n", current_task, (current_task + 1) % num_tasks);
    
        tasks[current_task].status = TASK_SLEEPING;

        // Perform the context switch
        printf("Change context from %p to %p\n", &current->context, &next->context);
        
        //swtch(&current->context, &next->context);
        
    }
    asm volatile("sti");
}

#define STACK_SIZE 64

// create a new task
void create_task(void (*entry_point)(void), uint32_t *stack) {
    task_t *task = &tasks[num_tasks++];
    // Initialize the kernel stack
    task->kernel_stack = stack;
    task->context.eip = (uint32_t)entry_point; // Set the entry point
    task->context.ebp = 0;                    // Clear EBP 
    task->status = TASK_READY;                // Mark as ready
}

void start_task(int task_id) {
    tasks[task_id].status = TASK_RUNNING;

    printf("Starting task %d with EIP=%p\n", task_id, tasks[task_id].context.eip);
    start_program_execution(tasks[task_id].context.eip);
}

void list_tasks() {
    printf("Task list:\n");
    // for (int i = 0; i < num_tasks; i++) {
    //     printf("Task %d: Name=%s, EIP=%p, ESP=%p, EBP=%p, Status=%s\n", i, tasks[i].name, tasks[i].eip, tasks[i].esp, tasks[i].ebp, tasks[i].status == TASK_RUNNING ? "Running" : "Ready");
    // }

    for (int i = 0; i < num_tasks; i++) {
        printf("Task %d: EIP=%p, EBP=%p, Status=%s\n", i, tasks[i].context.eip, tasks[i].context.ebp,
         tasks[i].status == TASK_RUNNING ? "Running" : "Ready");
    }
}
