#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>


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

void create_task(void (*entry_point)(void), uint32_t* stack, size_t stack_size);
// Task-Liste
volatile task_t tasks[MAX_TASKS] = {0};
volatile uint8_t current_task = 0; // ID des aktuellen Tasks
uint8_t num_tasks = 0;    // Anzahl der registrierten Tasks

// create a new task
void create_task(void (*entry_point)(void), uint32_t* stack, size_t stack_size) {
    uint32_t* stack_top = stack + stack_size / sizeof(uint32_t);

    // Push a "fake" return address (this will return to `schedule()`)
    //*(--stack_top) = (uint32_t)schedule;

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

void task1() {
    while (1) {
        printf("Task 1 running...\n");
    }
}

void task2() {
    while (1) {
        printf("Task 2 running...\n");
    }
}

void set_sp(uint32_t sp) {
    asm volatile(
        "movl %0, %%esp"  // Move the input value into ESP
        :                 // No output operands
        : "g"(sp)         // Input operand (allow memory or register operand)
        :                 // No clobbers (ESP is implicitly modified)
    );
}

uint32_t* get_sp() {
    uint32_t* sp;
    asm volatile(
        ".intel_syntax noprefix\n"  // Switch to Intel syntax
        "mov esp, %0\n"            // Use Intel-style instruction
        ".att_syntax prefix\n"     // Switch back to AT&T syntax
        : "=r" (sp)                // Output: 'sp' gets the value from ESP
        );
    return sp;
}


void schedule() {
    printf("Scheduler called\n");

    if(num_tasks == 0) {
        printf("No tasks to run\n");
        return;
    }

    // Save the current stack pointer
    tasks[current_task].stack_pointer = (uint32_t*)get_sp();

    // Find the next task that is ready to run
    uint8_t next_task = (current_task + 1) % num_tasks;
    while (tasks[next_task].status != TASK_READY) {
        next_task = (next_task + 1) % num_tasks;
    }

    // Update the current task
    tasks[current_task].status = TASK_READY;
    tasks[next_task].status = TASK_RUNNING;
    current_task = next_task;

    // Load the stack pointer of the next task
    //set_sp((uint32_t)tasks[current_task].stack_pointer);

    printf("Switching to task %d\n", current_task);

    // Jump to the next task
    //asm volatile("ret");
}

int main() {
    static uint32_t stack1[1024];
    static uint32_t stack2[1024];

    create_task(task1, stack1, sizeof(stack1));
    create_task(task2, stack2, sizeof(stack2));

    while (1) {
        schedule();
    }

    return 0;
}

