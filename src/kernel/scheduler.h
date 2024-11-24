#ifndef SCHEDULER_H
#define SCHEDULER_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Maximale Anzahl von Tasks
#define MAX_TASKS 8

#define TASK_READY 0
#define TASK_RUNNING 1
#define TASK_SLEEPING 2

typedef struct context {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t eip;
} context_t;

typedef struct task {
    uint32_t *kernel_stack; // Kernel stack pointer
    context_t context;      // Context for the task
    int status;             // Task status (e.g., TASK_READY, TASK_RUNNING)
    int is_started;         // Task started flag
} task_t;

extern task_t tasks[];

void create_task(void (*entry_point)(void), uint32_t *stack);
void start_task(int task_id);
void scheduler_interrupt_handler();
void list_tasks();

#endif // SCHEDULER_H