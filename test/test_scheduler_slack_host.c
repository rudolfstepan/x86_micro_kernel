#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/sched/scheduling_policy.h"
#include "kernel/sched/wait_queue.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "scheduler check failed line %d: %s\n", __LINE__, #x); \
    exit(1); } } while (0)
#define KASSERT(x) CHECK(x)
#define KASSERT_IRQ_DISABLED() ((void)0)
#define KASSERT_CAN_SLEEP() ((void)0)
#define X86_CPU_LOCAL_MAX 2U
#define SCHEDULER_HANDOFF_READY 1U
typedef struct { uint32_t generation; } Process;
typedef struct { uint32_t unused; } page_directory_t;
/* REAL_TYPES */
static task_t tasks[MAX_TASKS];
static uint8_t num_tasks;
static struct { uint32_t cpu_index; } local;
#define scheduler_cpu_local() (&local)
static int current_task;
static uint32_t preempt_disable_count;
static bool kernel_context_saved = true;
static context_t kernel_context;
static int task_table_lock;
static uint64_t clock_ms;
static unsigned switches, handoffs;
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t flags) { (void)flags; }
static void assert_task_table_locked(void) {}
static void spinlock_acquire(int *lock) { (void)lock; }
static void spinlock_release(int *lock) { (void)lock; }
static void validate_running_task_stack_or_panic(task_t *task) { (void)task; }
static uint64_t pit_monotonic_ms(void) { return clock_ms; }
static void activate_task_address_space(int index) { (void)index; }
static void prepare_task_handoff(int index, uint32_t action) {
    CHECK(action == SCHEDULER_HANDOFF_READY);
    tasks[index].status = TASK_READY;
    tasks[index].running_cpu = TASK_CPU_NONE;
    ++handoffs;
}
static void finish_task_handoff(void) {}
static void swtch(context_t *old, context_t *next) {
    (void)old; (void)next; ++switches;
}
/* REAL_GLOBALS */
/* REAL_FUNCTIONS */

static void reset(void) {
    memset(tasks, 0, sizeof(tasks));
    memset(cpu_windows, 0, sizeof(cpu_windows));
    memset(scheduler_cpu_policy_initialized, 0, sizeof(scheduler_cpu_policy_initialized));
    num_tasks = 0;
    current_task = -1;
    local.cpu_index = 0;
    clock_ms = 0;
    switches = handoffs = 0;
    for (uint32_t cpu = 0; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        local.cpu_index = cpu;
        (void)scheduler_cpu_policy_index_locked();
        scheduler_policy_window_init(&cpu_windows[cpu], 0);
    }
    local.cpu_index = 0;
}

static void add_task(uint8_t cls) {
    task_t *task = &tasks[num_tasks++];
    task->status = TASK_READY;
    task->running_cpu = TASK_CPU_NONE;
    task->cpu_affinity_mask = 3U;
    task->blocked_owner_task = -1;
    task->process_generation = num_tasks;
    task->task_generation = num_tasks;
    task->scheduling_class = task->effective_scheduling_class = cls;
    task->budget_remaining = scheduler_policy_budget(cls);
}

static void regression_slack(void) {
    reset();
    add_task(SCHEDULER_CLASS_AMBIENT);
    current_task = claim_next_runnable(-1, 0);
    CHECK(current_task == 0);
    CHECK(find_next_runnable(0, 15) == 0);
    CHECK(cpu_windows[0].used_ms[0] == 15);
    CHECK(cpu_windows[0].used_ms[1] == 0 && cpu_windows[0].used_ms[2] == 0);
    clock_ms = 30;
    CHECK(scheduler_yield() == 0);
    CHECK(current_task == 0 && switches == 0 && tasks[0].status == TASK_RUNNING);
    CHECK(cpu_windows[0].used_ms[0] == 30);
    add_task(SCHEDULER_CLASS_SERVICE);
    clock_ms = 31;
    CHECK(scheduler_yield() == 0);
    CHECK(current_task == 1 && switches == 1 && handoffs == 1);
    CHECK(cpu_windows[0].used_ms[0] == 31 && cpu_windows[0].used_ms[1] == 0);
    account_current_runtime_locked(40);
    CHECK(cpu_windows[0].used_ms[1] == 9);
    tasks[1].status = TASK_SLEEPING;
    tasks[1].running_cpu = TASK_CPU_NONE;
    current_task = -1;
    account_current_runtime_locked(90);
    CHECK(cpu_windows[0].used_ms[1] == 9); /* no idle debit */
    CHECK(find_next_runnable(-1, 100) == 0);
    CHECK(cpu_windows[0].used_ms[0] == 0 && cpu_windows[0].used_ms[1] == 0);
}

static void regression_accounting(void) {
    reset();
    add_task(SCHEDULER_CLASS_AMBIENT);
    current_task = claim_next_runnable(-1, 0);
    add_task(SCHEDULER_CLASS_SAFETY);
    tasks[1].status = TASK_WAITING;
    tasks[1].blocked_owner_task = 0;
    tasks[1].blocked_owner_generation = tasks[0].process_generation;
    /* Another CPU can refresh inheritance while CPU0 still runs Ambient. */
    local.cpu_index = 1;
    refresh_effective_classes_locked();
    CHECK(tasks[0].effective_scheduling_class == SCHEDULER_CLASS_SAFETY);
    local.cpu_index = 0;
    account_current_runtime_locked(12);
    CHECK(cpu_windows[0].used_ms[0] == 12 && cpu_windows[0].used_ms[2] == 0);
    CHECK(claim_next_runnable(0, 12) == 0);
    tasks[1].blocked_owner_generation++; /* stale inheritance withdrawn */
    refresh_effective_classes_locked();
    account_current_runtime_locked(20);
    CHECK(cpu_windows[0].used_ms[0] == 12 && cpu_windows[0].used_ms[2] == 8);
    CHECK(cpu_windows[1].used_ms[0] == 0 && cpu_windows[1].used_ms[2] == 0);
    CHECK(claim_next_runnable(0, 20) == 0);
    tasks[0].status = TASK_FINISHED;
    account_current_runtime_locked(25); /* exit still charges the pinned task */
    CHECK(cpu_windows[0].used_ms[0] == 17);
    current_task = -1;
    CHECK(find_next_runnable(-1, 30) == -1);
    tasks[0].task_generation++;
    tasks[0].running_cpu = TASK_CPU_NONE;
    tasks[0].status = TASK_READY;
    CHECK(claim_next_runnable(-1, 30) == 0); /* valid new claim, not a latch */
}

static void exhaustive_selection(void) {
    for (unsigned mask = 0; mask < 8; ++mask) {
        for (unsigned ready = 0; ready < 8; ++ready) {
            for (unsigned cycle = 0; cycle < 4; ++cycle) {
                reset();
                scheduler_candidate_t funded[3] = {{0}};
                int8_t cursors[3] = {-1, -1, -1};
                uint8_t cursor = (uint8_t)cycle;
                cpu_windows[0].throttled_mask = (uint8_t)mask;
                scheduling_class_cycle_cursors[0] = cursor;
                for (unsigned cls = 0; cls < 3; ++cls) {
                    add_task((uint8_t)cls);
                    if (!(ready & (1U << cls))) tasks[cls].status = TASK_SLEEPING;
                    funded[cls] = (scheduler_candidate_t){
                        (ready & (1U << cls)) && !(mask & (1U << cls)),
                        (uint8_t)cls, 1};
                }
                int expected = scheduler_policy_select_cycle(funded, 3, cursors, &cursor);
                int got = find_next_runnable(-1, 0);
                if (expected >= 0) CHECK(got == expected);
                else if (!(mask & 4) && (ready & 3)) {
                    CHECK(got >= 0 && got < 2 && (ready & (1U << got)));
                } else CHECK(got == -1);
                CHECK(cpu_windows[0].throttled_mask == mask);
                cpu_windows[0].fault_flags = SCHEDULER_WINDOW_FAULT_CLOCK_REGRESSION;
                CHECK(find_next_runnable(-1, 0) == -1);
            }
        }
    }
    /* Background uses the existing round robin, including full slot capacity. */
    reset();
    for (unsigned i = 0; i < MAX_TASKS; ++i) add_task(SCHEDULER_CLASS_AMBIENT);
    cpu_windows[0].throttled_mask = 1;
    for (unsigned i = 0; i < MAX_TASKS * 2; ++i)
        CHECK(find_next_runnable(-1, 0) == (int)(i % MAX_TASKS));
}

static void ownership_and_faults(void) {
    reset();
    scheduler_window_t uninitialized = {0};
    CHECK(!scheduler_policy_background_allowed(&uninitialized, 0));
    CHECK(!scheduler_policy_background_allowed(NULL, 0));
    CHECK(!scheduler_policy_background_allowed(&cpu_windows[0], 3));
    CHECK(!scheduler_policy_background_allowed(&cpu_windows[0], 255));
    add_task(SCHEDULER_CLASS_AMBIENT);
    add_task(SCHEDULER_CLASS_SERVICE);
    cpu_windows[0].throttled_mask = 1;
    tasks[1].running_cpu = 1; tasks[1].status = TASK_RUNNING;
    CHECK(find_next_runnable(-1, 0) == 0);
    tasks[0].cpu_affinity_mask = 2;
    CHECK(find_next_runnable(-1, 0) == -1);
    tasks[0].cpu_affinity_mask = 3;
    tasks[0].status = TASK_HANDOFF;
    CHECK(find_next_runnable(-1, 0) == -1);
    tasks[0].status = TASK_READY;
    current_task = claim_next_runnable(-1, 0);
    CHECK(current_task == 0);
    CHECK(find_next_runnable(0, 50) == 0);
    CHECK(find_next_runnable(0, 49) == -1);
    clock_ms = 49;
    CHECK(scheduler_yield() == 0 && current_task == -1 && switches == 1);
    CHECK(find_next_runnable(-1, 1000) == -1); /* latched across rollover */
    CHECK(cpu_windows[0].fault_flags != 0);
    scheduler_policy_window_init(&cpu_windows[0], 1000);
    current_task = claim_next_runnable(-1, 1000);
    CHECK(current_task == 0);
    CHECK(find_next_runnable(0, UINT64_MAX) == 0);
    CHECK(cpu_windows[0].overload_count[0] == UINT32_MAX);
    CHECK(cpu_windows[0].used_ms[0] == UINT64_MAX % 100);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "slack")) regression_slack();
    else if (!strcmp(argv[1], "accounting")) regression_accounting();
    else if (!strcmp(argv[1], "selection")) exhaustive_selection();
    else if (!strcmp(argv[1], "faults")) ownership_and_faults();
    else CHECK(false);
    puts("SCHEDULER_SLACK_OK");
    return 0;
}
