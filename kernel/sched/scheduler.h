/**
 * @file kernel/sched/scheduler.h
 * @brief Scheduler- und Kontextwechselvertrag.
 *
 * Layer: Ring-0 scheduler.
 * Contract: Ein Task besitzt höchstens einen aktiven intrinsischen Wait-Node.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "kernel/proc/process.h"
#include "kernel/sched/scheduling_policy.h"
#include "kernel/sched/wait_queue.h"
#include "arch/x86/mm/paging.h"
#include "include/lib/spinlock.h"

/* Fixed capacity keeps scheduler scans and stack storage bounded.  Thirty-two
 * slots leave room for supervised services, the shell, the desktop and its
 * applications while preserving the dedicated recovery reserve. */
#define MAX_TASKS 32
#define SUPERVISED_TASK_RESERVE 1U
#define SCHEDULER_HELD_MUTEX_CAPACITY 8U

#define TASK_READY 0
#define TASK_RUNNING 1
#define TASK_SLEEPING 2
#define TASK_FINISHED 3
#define TASK_WAITING 4
#define TASK_REAPING 5
#define TASK_HANDOFF 6
#define TASK_PREPARED 7
#define TASK_CPU_NONE (-1)
#define TASK_CPU_MASK_BSP 1U

#define STACK_SIZE (8U * 1024U)

#define SCHEDULER_RESOURCE_STATS_VERSION 1U
#define RUNTIME_TIMING_STATS_VERSION 1U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t task_capacity;
    uint32_t active_tasks;
    uint32_t peak_active_tasks;
    uint32_t capacity_rejections;
    uint32_t supervised_reserve;
    uint32_t reserved;
} scheduler_resource_stats_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint64_t cpu_frequency_hz;
    uint64_t scheduler_samples;
    uint64_t scheduler_total_cycles;
    uint64_t scheduler_max_cycles;
    uint64_t syscall_samples;
    uint64_t syscall_total_cycles;
    uint64_t syscall_max_cycles;
    uint64_t clock_anomalies;
} runtime_timing_stats_t;


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
    uint32_t process_generation;
    uint32_t task_generation; /* Never-zero incarnation of this task slot. */
    page_directory_t *page_directory;
    uint32_t *reap_kernel_stack;
    page_directory_t *reap_page_directory;
    uint32_t user_entry;
    uint32_t user_stack;
    bool user_mode;
    wait_queue_node_t wait_node;
    uint64_t wait_deadline_ms;
    int wait_result;
    uint8_t scheduling_class;
    uint8_t effective_scheduling_class;
    uint8_t budget_remaining;
    int8_t blocked_owner_task;
    volatile int32_t running_cpu;
    uint32_t cpu_affinity_mask;
    uint32_t blocked_owner_generation;
    /* Distinct first-acquisition records only. Recursive depth remains in
     * the mutex. Fixed storage lets forced termination revoke an abandoned
     * task generation before cleanup needs another filesystem lock. */
    void *held_mutexes[SCHEDULER_HELD_MUTEX_CAPACITY];
    uint32_t held_mutex_count;
} task_t;

typedef enum {
    TASK_BLOCK_WAITING,
    TASK_BLOCK_SLEEPING
} task_block_kind_t;

int create_task(void (*entry_point)(void), uint32_t *stack, Process *process);
int create_affined_kernel_task(void (*entry_point)(void), uint32_t *stack,
                               uint32_t cpu_affinity_mask);
int create_user_task(uint32_t entry_point, uint32_t user_stack,
                     uint32_t *kernel_stack, page_directory_t *page_directory,
                     Process *process);
int create_supervised_user_task(uint32_t entry_point, uint32_t user_stack,
                                uint32_t *kernel_stack,
                                page_directory_t *page_directory,
                                Process *process);
int create_affined_supervised_user_task(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process,
    uint32_t cpu_affinity_mask);
int create_prepared_supervised_user_task(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process);
int scheduler_start_prepared_user_task_locked(
    int task_id, const Process *owner, uint32_t process_generation);
int scheduler_set_task_affinity_locked(
    int task_id, const Process *owner, uint32_t process_generation,
    uint32_t cpu_affinity_mask);
uint32_t* scheduler_allocate_kernel_stack(void);
void scheduler_free_kernel_stack(uint32_t* stack);
bool scheduler_kernel_stack_is_valid(const uint32_t* stack);
bool scheduler_kernel_context_stack_is_valid(void);
int scheduler_reap_finished_task_locked(int task_id, const Process* owner);
size_t scheduler_reap_finished_tasks(void);
void scheduler_interrupt_handler(void);
uint64_t runtime_timing_begin(void);
void runtime_timing_record_syscall(uint64_t start_cycles);
int scheduler_runtime_timing_stats(runtime_timing_stats_t *stats_out);
void scheduler_preempt_disable(void);
void scheduler_preempt_enable(void);
bool scheduler_preempt_is_disabled(void);
bool scheduler_can_sleep(void);
void scheduler_terminate_task(int task_id);
int wait_queue_block_locked(wait_queue_t *queue, task_block_kind_t kind);
int wait_queue_block_until_locked(wait_queue_t *queue,
                                  task_block_kind_t kind,
                                  uint64_t deadline_ms);
/**
 * Atomically transfer from a held condition lock into a scheduler wait queue.
 * Releases condition_lock and restores irq_flags on every return path.
 */
int wait_queue_block_until_spinlocked(wait_queue_t *queue,
                                      task_block_kind_t kind,
                                      uint64_t deadline_ms,
                                      spinlock_t *condition_lock,
                                      uint32_t irq_flags);
bool wait_queue_wake_one_locked(wait_queue_t *queue);
size_t wait_queue_wake_all_locked(wait_queue_t *queue);
void wait_queue_cancel_locked(task_t *task);
int scheduler_sleep_ms(uint32_t milliseconds);
int scheduler_yield(void);
int scheduler_current_task_id(void);
bool scheduler_current_task_identity(int *task_id_out,
                                     uint32_t *generation_out);
int scheduler_mutex_owner_register(int task_id, uint32_t task_generation,
                                   void *mutex);
int scheduler_mutex_owner_unregister(int task_id, uint32_t task_generation,
                                     void *mutex);
int scheduler_task_state_snapshot(int task_id, const Process *owner,
                                  uint32_t generation, int *state_out);
void scheduler_wake_expired_sleepers_locked(uint64_t now_ms);
void scheduler_wake_expired_waiters_locked(uint64_t now_ms);
bool scheduler_set_wait_owner_locked(int pid, uint32_t generation);
void scheduler_clear_wait_owner_locked(void);
void scheduler_set_apic_timer_active(bool active);
bool scheduler_uses_pit_fallback(void);
void scheduler_pit_interrupt_handler(void);
void task_exit(void) __attribute__((noreturn));
void task_exit_status(int status) __attribute__((noreturn));
void scheduler_kill_current(void) __attribute__((noreturn));
void list_tasks(void);
Process* scheduler_current_process(void);
int scheduler_resource_stats(scheduler_resource_stats_t *stats_out);

#endif // SCHEDULER_H
