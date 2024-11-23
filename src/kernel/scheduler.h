#ifndef SCHEDULER_H
#define SCHEDULER_H


#include <stdint.h>
#include <stddef.h>

// Maximale Anzahl von Tasks
#define MAX_TASKS 8

// Task-Status
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING
} task_status_t;

// Task-Control-Block (TCB)
typedef struct {
    uint32_t* stack_pointer;  // Zeiger auf den aktuellen Stack
    task_status_t status;     // Task-Status
    void (*entry_point)(void); // Einstiegspunkt der Task
} task_t;


void schedule();
void create_task(void (*entry_point)(void), uint32_t* stack, size_t stack_size);

void scheduler_interrupt_handler();

#endif // SCHEDULER_H