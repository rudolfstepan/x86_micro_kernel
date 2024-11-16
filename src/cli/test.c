#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"

#include <stdint.h>
#include <stdbool.h>



// Task states
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING
} TaskState;

// Task structure
typedef struct {
    void (*taskFunction)(void*); // Pointer to the task function
    void* taskData;              // Task-specific data
    TaskState state;             // Current state of the task
    uint32_t delay;              // Delay counter for async waiting
} Task;

// Task list and scheduler
#define MAX_TASKS 5
Task taskList[MAX_TASKS] = {0};
uint8_t taskCount = 0;


// Function prototypes
bool addTask(void (*function)(void*), void* data);
void taskDelay(uint32_t ticks);
void schedulerTick(void);
void blinkLED(void* data);
void monitorSensor(void* data);

TryContext ctx;
//TryContext* current_try_context = NULL;

int test_divide_by_zero() {
    int x = 10;
    int y = 0;
    int z = x / y; // Trigger a divide-by-zero exception
    x = z+1;
    return z;
}

int _main() {
    current_try_context = &ctx; // Set the global context pointer

    if (setjmp(&ctx) == 0) {
        test_divide_by_zero();
        printf("Try block executed successfully\n");
    } else {
        printf("Caught divide-by-zero exception\n");
    }

    current_try_context = NULL; // Clear the context pointer
    printf("Program execution continues...\n");

    return 0;
}
// Add a new task to the scheduler
bool addTask(void (*function)(void*), void* data) {
    if (taskCount >= MAX_TASKS) {
        return false; // Task list is full
    }
    taskList[taskCount++] = (Task){function, data, TASK_READY, 0};
    return true;
}

// Delay a task for a specified number of ticks
void taskDelay(uint32_t ticks) {
    for (int i = 0; i < taskCount; i++) {
        if (taskList[i].state == TASK_RUNNING) {
            taskList[i].state = TASK_WAITING;
            taskList[i].delay = ticks;
            break;
        }
    }
}

// Task scheduler (cooperative)
void schedulerTick(void) {
    for (int i = 0; i < taskCount; i++) {
        if (taskList[i].state == TASK_WAITING && taskList[i].delay > 0) {
            taskList[i].delay--;
            if (taskList[i].delay == 0) {
                taskList[i].state = TASK_READY;
            }
        }
    }

    for (int i = 0; i < taskCount; i++) {
        if (taskList[i].state == TASK_READY) {
            taskList[i].state = TASK_RUNNING;
            taskList[i].taskFunction(taskList[i].taskData);
            if (taskList[i].state == TASK_RUNNING) {
                taskList[i].state = TASK_READY;
            }
        }
    }
}

// Example task 1
void blinkLED(void* data) {
    static uint32_t counter = 0;
    printf("Task 1: Blinking LED (counter = %u)\n", ++counter);
    taskDelay(5); // Delay for 5 ticks
}

// Example task 2
void monitorSensor(void* data) {
    static uint32_t counter = 0;
    printf("Task 2: Monitoring sensor (counter = %u)\n", ++counter);
    taskDelay(3); // Delay for 3 ticks
}


void main() __attribute__((section(".text.main")));
void main() {
    // Main program logic
    _main();
}
