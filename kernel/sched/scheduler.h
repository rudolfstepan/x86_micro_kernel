#ifndef SCHEDULER_H
#define SCHEDULER_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "kernel/proc/process.h"
//#include "arch/x86/paging.h"

// Maximale Anzahl von Tasks
#define MAX_TASKS 8

#define TASK_READY 0
#define TASK_RUNNING 1
#define TASK_SLEEPING 2
#define TASK_FINISHED 3

#define STACK_SIZE 1024*8 // 8 KB


typedef struct {
    uint32_t esp;  // Stack-Pointer
    uint32_t ebp;  // Base-Pointer
    uint32_t ebx;  // Callee-saved Register
    uint32_t esi;  // Callee-saved Register
    uint32_t edi;  // Callee-saved Register
    uint32_t eip;  // Instruction Pointer
} context_t;

typedef struct task {
    uint32_t *kernel_stack; // Kernel stack pointer
    context_t context;      // Context for the task
    int status;             // Task status (e.g., TASK_READY, TASK_RUNNING)
    int is_started;         // Task started flag
    Process *process;       // Process associated with the task
    //page_directory_t *page_directory; // Pointer to the task's page directory
} task_t;

extern task_t tasks[];

void create_task(void (*entry_point)(void), uint32_t *stack, Process *process);
void scheduler_interrupt_handler();
void list_tasks();

#endif // SCHEDULER_H