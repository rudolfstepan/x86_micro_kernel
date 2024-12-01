#include "scheduler.h"
#include "process.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"
//#include "paging.h"


// Deklaration der Assembly-Funktion für den Kontextwechsel
extern void swtch(context_t *old, context_t *new);

// Task-Liste
task_t tasks[MAX_TASKS];
volatile uint8_t current_task = 0; // ID des aktuellen Tasks
uint8_t num_tasks = 0;             // Anzahl der registrierten Tasks


// // Helper: Load CR3 (Page Directory Base Register)
// static inline void load_cr3(uint32_t address) {
//     asm volatile("mov %0, %%cr3" : : "r"(address));
// }


void task_exit() {
    printf("Task %d finished execution.\n", current_task);

    tasks[current_task].status = TASK_FINISHED;

    // Suche den nächsten Task
    int active_tasks = 0;
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].status != TASK_FINISHED) {
            active_tasks++;
        }
    }

    if (active_tasks == 0) {
        printf("All tasks are finished. Halting system.\n");
        while (1); // System anhalten
    }

    // Finde den nächsten Task
    while (tasks[current_task].status == TASK_FINISHED) {
        current_task = (current_task + 1) % num_tasks;
    }

    task_t *next = &tasks[current_task];
    swtch(NULL, &next->context);

    printf("ERROR: Returned to a finished task!\n");
    while (1);
}

// void task_exit() {
//     printf("Task %d finished execution.\n", current_task);

//     // Mark the current task as finished
//     tasks[current_task].status = TASK_FINISHED;

//     // Clean up the task's page directory if necessary
//     if (tasks[current_task].page_directory) {
//         free_page_directory(tasks[current_task].page_directory);
//         tasks[current_task].page_directory = NULL;
//     }

//     // Check if any active tasks remain
//     int active_tasks = 0;
//     for (int i = 0; i < num_tasks; i++) {
//         if (tasks[i].status != TASK_FINISHED) {
//             active_tasks++;
//         }
//     }

//     if (active_tasks == 0) {
//         printf("All tasks are finished. Halting system.\n");
//         while (1); // Halt the system
//     }

//     // Find the next task to execute
//     do {
//         current_task = (current_task + 1) % num_tasks;
//     } while (tasks[current_task].status == TASK_FINISHED);

//     task_t *next = &tasks[current_task];

//     // Switch to the next task's page directory
//     load_cr3((uint32_t)next->page_directory);

//     printf("Switching to task %d\n", current_task);

//     // Perform a context switch
//     swtch(NULL, &next->context);

//     // If control returns here, something went wrong
//     printf("ERROR: Returned to a finished task!\n");
//     while (1);
// }


// Scheduler
void scheduler_interrupt_handler() {
    asm volatile("cli");

    task_t *current = &tasks[current_task];

    // Wähle den nächsten Task, der nicht beendet ist
    do {
        current_task = (current_task + 1) % num_tasks;
    } while (tasks[current_task].status == TASK_FINISHED);

    task_t *next = &tasks[current_task];

    if (!next->is_started) {
        // Initialisiere neuen Task
        next->is_started = 1;
        next->status = TASK_RUNNING;

        uint32_t *stack_top = (uint32_t *)(next->kernel_stack + STACK_SIZE);
        *(--stack_top) = (uint32_t)task_exit;        // Rücksprungadresse
        *(--stack_top) = (uint32_t)next->context.eip; // Startadresse
        next->context.esp = (uint32_t)stack_top;     // Stack-Pointer setzen
    }

    // Führe den Kontextwechsel durch
    swtch(&current->context, &next->context);

    asm volatile("sti");
}

// void scheduler_interrupt_handler() {
//     asm volatile("cli");

//     task_t *current = &tasks[current_task];

//     // Find the next task
//     do {
//         current_task = (current_task + 1) % num_tasks;
//     } while (tasks[current_task].status == TASK_FINISHED);

//     task_t *next = &tasks[current_task];

//     if (!next->is_started) {
//         next->is_started = 1;
//         next->status = TASK_RUNNING;

//         uint32_t *stack_top = (uint32_t *)(next->kernel_stack + STACK_SIZE);
//         *(--stack_top) = (uint32_t)task_exit;
//         *(--stack_top) = (uint32_t)next->context.eip;
//         next->context.esp = (uint32_t)stack_top;
//     }

//     // Switch page directory
//     load_cr3((uint32_t)next->page_directory);

//     // Perform the context switch
//     swtch(&current->context, &next->context);

//     asm volatile("sti");
// }

// void free_page_directory(page_directory_t* pd) {
//     if (!pd) return;

//     // Free all user page tables
//     for (int i = USER_PAGE_START; i < PAGE_DIRECTORY_ENTRIES; i++) {
//         if (pd->entries[i] & PAGE_PRESENT) {
//             uint32_t* pt = (uint32_t*)(pd->entries[i] & ~0xFFF);
//             free_page(pt); // Free the page table
//         }
//     }

//     free_page(pd); // Free the page directory itself
// }


void create_task(void (*entry_point)(void), uint32_t *stack, Process *process) {
    if (num_tasks >= MAX_TASKS) {
        printf("Error: Maximum number of tasks reached!\n");
        return;
    }

    task_t *task = &tasks[num_tasks++];
    memset(task, 0, sizeof(task_t)); // Setze alle Felder auf 0

    task->kernel_stack = stack;
    task->context.eip = (uint32_t)entry_point;
    task->context.esp = (uint32_t)(stack + STACK_SIZE); // Stack-Ende
    task->status = TASK_READY;
    task->is_started = 0;
    task->process = process;
}

// void create_task(void (*entry_point)(void), uint32_t *stack, Process *process) {
//     if (num_tasks >= MAX_TASKS) {
//         printf("Error: Maximum number of tasks reached!\n");
//         return;
//     }

//     task_t *task = &tasks[num_tasks++];
//     memset(task, 0, sizeof(task_t)); // Initialize task to 0

//     task->kernel_stack = stack;                       // Assign stack
//     task->context.eip = (uint32_t)entry_point;        // Entry point
//     task->context.esp = (uint32_t)(stack + STACK_SIZE); // Stack pointer
//     task->status = TASK_READY;                        // Set initial status
//     task->is_started = 0;                             // Mark as not started
//     task->process = process;

//     // Assign a new page directory to the task
//     task->page_directory = create_page_directory();
//     if (!task->page_directory) {
//         printf("Error: Failed to create page directory for task.\n");
//         num_tasks--;
//         return;
//     }
// }

void list_tasks() {
    printf("Task list:\n");
    for (int i = 0; i < num_tasks; i++) {
        printf("Task %d: EIP=%p, ESP=%p, Status=%s\n", i, tasks[i].context.eip, tasks[i].context.esp,
               tasks[i].status == TASK_RUNNING ? "Running" : "Ready");
    }
}
