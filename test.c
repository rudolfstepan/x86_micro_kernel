#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>

// Anzahl der Tasks
#define NUM_TASKS 2
#define STACK_SIZE (1024 * 4) // 4 KB Stack

typedef struct {
    jmp_buf context;       // Kontextspeicher für den Task
    uint8_t *stack;        // Zeiger auf den Task-Stack
    void (*task_func)();   // Funktion, die der Task ausführt
    int active;            // Status, ob der Task aktiv ist
    int id;                // Task-ID
} Task;

Task tasks[NUM_TASKS];
int current_task = -1;

// Kontextwechsel
void switch_task() {
    int prev_task = current_task;
    current_task = (current_task + 1) % NUM_TASKS;

    // Überspringe inaktive Tasks
    while (!tasks[current_task].active) {
        current_task = (current_task + 1) % NUM_TASKS;
    }

    if (!setjmp(tasks[prev_task].context)) {
        longjmp(tasks[current_task].context, 1);
    }
}

// Task-Wrapper
void task_wrapper() {
    tasks[current_task].task_func();

    // Task ist beendet, markiere ihn als inaktiv
    tasks[current_task].active = 0;
    free(tasks[current_task].stack); // Stack freigeben
    switch_task();
}

// Task initialisieren
void init_task(int id, void (*task_func)()) {
    printf("Versuche, %d Bytes Speicher für Task %d zuzuweisen...\n", STACK_SIZE, id);
    tasks[id].stack = malloc(STACK_SIZE);
    if (!tasks[id].stack) {
        fprintf(stderr, "Fehler: Speicherzuweisung für Task %d fehlgeschlagen\n", id);
        exit(EXIT_FAILURE);
    }

    tasks[id].task_func = task_func;
    tasks[id].active = 1;
    tasks[id].id = id;

    if (!setjmp(tasks[id].context)) {
        printf("Task %d initialisiert\n", id);
        return;
    }

    printf("Nach setjmp: current_task = %d, Task-ID = %d\n", current_task, tasks[current_task].id);

    // Wechsel auf den neuen Stack
    uint8_t *stack_top = tasks[current_task].stack + STACK_SIZE;

#ifdef __x86_64__
    asm volatile("mov %0, %%rsp" :: "r"(stack_top));
#else
#error "Architektur wird nicht unterstützt"
#endif

    task_wrapper();
}

// Tasks
void task1() {
    while (1) {
        printf("Task 1 läuft\n");
        for (volatile int i = 0; i < 1000000000; i++); // Dummy-Delay
        switch_task();
    }
}

void task2() {
    while (1) {
        printf("Task 2 läuft\n");
        for (volatile int i = 0; i < 1000000000; i++); // Dummy-Delay
        switch_task();
    }
}

// Main
int main() {
    // Initialisiere Tasks
    init_task(0, task1);
    init_task(1, task2);

    // Starte den ersten Task
    current_task = 0;
    longjmp(tasks[0].context, 1);

    return 0;
}
