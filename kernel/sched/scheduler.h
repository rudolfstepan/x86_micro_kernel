#ifndef SCHEDULER_H
#define SCHEDULER_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "kernel/proc/process.h"
#include "arch/x86/mm/paging.h"

// Maximale Anzahl von Tasks
#define MAX_TASKS 8

#define TASK_READY 0
#define TASK_RUNNING 1
#define TASK_SLEEPING 2
#define TASK_FINISHED 3

#define STACK_SIZE (8U * 1024U)


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
    page_directory_t *page_directory;
    uint32_t user_entry;
    uint32_t user_stack;
    bool user_mode;
} task_t;

extern task_t tasks[];
extern volatile int current_task;
extern uint8_t num_tasks;

int create_task(void (*entry_point)(void), uint32_t *stack, Process *process);
int create_user_task(uint32_t entry_point, uint32_t user_stack,
                     uint32_t *kernel_stack, page_directory_t *page_directory,
                     Process *process);
void scheduler_interrupt_handler(void);
void scheduler_preempt_disable(void);
void scheduler_preempt_enable(void);
void scheduler_terminate_task(int task_id);
void task_exit(void) __attribute__((noreturn));
void scheduler_kill_current(void) __attribute__((noreturn));
void list_tasks(void);
Process* scheduler_current_process(void);

#endif // SCHEDULER_H
