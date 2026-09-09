/**
 * @file kernel/sched/scheduler.c
 * @brief Deterministischer Prozess-Scheduler.
 *
 * Layer: Ring-0 scheduler.
 * Contract: Nur READY-Prozesse laufen; Blockierung wahrt Wait-Node-Ownership.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#include "kernel/sched/scheduler.h"

#include "arch/x86/include/interrupt.h"
#include "arch/x86/include/cpu_local.h"
#include "arch/x86/include/fpu.h"
#include "arch/x86/include/smp.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/include/tss.h"
#include "arch/x86/mm/paging.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "include/kernel/panic.h"
#include "kernel/time/pit.h"
#include "include/kernel/watchdog.h"
#include "include/kernel/ipc.h"
#include "include/kernel/device_domain.h"
#include "include/kernel/storage_request_pool.h"
#include "include/lib/spinlock.h"
#include "kernel/sched/mutex.h"
#include "drivers/video/framebuffer.h"
#include "mm/kmalloc.h"

extern void swtch(context_t *old, context_t *new);
extern void enter_user_mode(uint32_t entry_point, uint32_t user_stack)
    __attribute__((noreturn));

task_t tasks[MAX_TASKS];
uint8_t num_tasks = 0;
static uint32_t next_task_generation;

static x86_cpu_local_t *scheduler_cpu_local(void) {
    x86_cpu_local_t *local = x86_cpu_local_current();
    KASSERT(local != NULL);
    KASSERT(local->online != 0U);
    return local;
}

#define current_task (scheduler_cpu_local()->scheduler_current_task)
#define kernel_context \
    (*(context_t *)(void *)scheduler_cpu_local()->scheduler_context)
#define kernel_context_saved \
    (scheduler_cpu_local()->scheduler_context_saved)
#define preempt_disable_count \
    (x86_cpu_local_current()->preempt_disable_count)
#define preemption_pending \
    (x86_cpu_local_current()->preemption_pending)
#define handoff_task \
    (scheduler_cpu_local()->scheduler_handoff_task)
#define handoff_action \
    (scheduler_cpu_local()->scheduler_handoff_action)
static bool apic_timer_active;
static spinlock_t task_table_lock = SPINLOCK_INIT;
static spinlock_t runtime_timing_lock = SPINLOCK_INIT;
static uint32_t pit_scheduler_ticks;
static wait_queue_t sleep_waiters = WAIT_QUEUE_INIT;
static scheduler_window_t cpu_windows[X86_CPU_LOCAL_MAX];
/* Accounting only: the CPU/task claim remains the execution authority. */
static uint8_t dispatched_classes[X86_CPU_LOCAL_MAX];
static int8_t
    scheduling_class_cursors[X86_CPU_LOCAL_MAX][SCHEDULER_CLASS_COUNT];
static uint8_t scheduling_class_cycle_cursors[X86_CPU_LOCAL_MAX];
static bool scheduler_cpu_policy_initialized[X86_CPU_LOCAL_MAX];
static uint32_t peak_active_tasks;
static uint32_t task_capacity_rejections;
static runtime_timing_stats_t runtime_timing_stats;

_Static_assert(MAX_TASKS <= SCHEDULER_POLICY_MAX_CANDIDATES,
               "scheduler policy candidate capacity is too small");
_Static_assert(KERNEL_STACK_SLOT_COUNT >=
                   MAX_TASKS + X86_CPU_LOCAL_MAX - 1U,
               "kernel-stack arena cannot preserve task capacity under SMP");
_Static_assert(sizeof(scheduler_resource_stats_t) == 32U,
               "scheduler statistics ABI size changed");
_Static_assert(sizeof(runtime_timing_stats_t) == 72U,
               "runtime timing statistics ABI size changed");
_Static_assert(sizeof(context_t) ==
                   X86_SCHEDULER_CONTEXT_WORDS * sizeof(uint32_t),
               "per-CPU scheduler context layout changed");
_Static_assert(offsetof(context_t,fpu_state)==X86_FPU_CONTEXT_OFFSET &&
               sizeof(((context_t *)0)->fpu_state)==X86_FPU_STATE_BYTES &&
               _Alignof(context_t)==16 && offsetof(task_t,context)%16==0,
               "FXSAVE switch layout/alignment changed");

#define SCHEDULER_QUANTUM_MS 10U
#define KERNEL_STACK_GUARD 0x4B535447U /* "KSTG" */
#define KERNEL_STACK_WATERMARK_BYTE 0xA5U
#define SCHEDULER_HANDOFF_NONE 0U
#define SCHEDULER_HANDOFF_READY 1U
#define SCHEDULER_HANDOFF_RELEASE 2U

static uint32_t task_table_lock_irqsave(void) {
    return spinlock_acquire_irq(&task_table_lock);
}

static void task_table_unlock_irqrestore(uint32_t flags) {
    spinlock_release_irq(&task_table_lock, flags);
}

static void assert_task_table_locked(void) {
    KASSERT_IRQ_DISABLED();
    KASSERT(spinlock_is_owned_by_current(&task_table_lock));
}

static uint32_t scheduler_cpu_policy_index_locked(void) {
    assert_task_table_locked();
    uint32_t cpu = scheduler_cpu_local()->cpu_index;
    KASSERT(cpu < X86_CPU_LOCAL_MAX);
    if (!scheduler_cpu_policy_initialized[cpu]) {
        dispatched_classes[cpu] = SCHEDULER_CLASS_NONE;
        for (uint32_t scheduling_class = 0U;
             scheduling_class < SCHEDULER_CLASS_COUNT; ++scheduling_class)
            scheduling_class_cursors[cpu][scheduling_class] = -1;
        scheduling_class_cycle_cursors[cpu] = 0U;
        scheduler_cpu_policy_initialized[cpu] = true;
    }
    return cpu;
}

static uint64_t saturating_increment_u64(uint64_t value) {
    return value == UINT64_MAX ? UINT64_MAX : value + 1U;
}

static uint64_t saturating_add_u64(uint64_t value, uint64_t increment) {
    return UINT64_MAX - value < increment ? UINT64_MAX : value + increment;
}

static void runtime_timing_record(uint64_t start_cycles, bool scheduler) {
    uint64_t end_cycles = cpu_cycle_counter_read();
    uint32_t flags = spinlock_acquire_irq(&runtime_timing_lock);
    /* Scheduler IRQs start before boot-time frequency calibration. Those
     * early intervals cannot be normalized and are deliberately not samples;
     * only a backwards calibrated counter is a persistent anomaly. */
    if (cpu_frequency == 0U) {
        spinlock_release_irq(&runtime_timing_lock, flags);
        return;
    }
    if (end_cycles < start_cycles) {
        runtime_timing_stats.clock_anomalies = saturating_increment_u64(
            runtime_timing_stats.clock_anomalies);
        spinlock_release_irq(&runtime_timing_lock, flags);
        return;
    }
    uint64_t elapsed = end_cycles - start_cycles;
    uint64_t *samples = scheduler
        ? &runtime_timing_stats.scheduler_samples
        : &runtime_timing_stats.syscall_samples;
    uint64_t *total = scheduler
        ? &runtime_timing_stats.scheduler_total_cycles
        : &runtime_timing_stats.syscall_total_cycles;
    uint64_t *maximum = scheduler
        ? &runtime_timing_stats.scheduler_max_cycles
        : &runtime_timing_stats.syscall_max_cycles;
    *samples = saturating_increment_u64(*samples);
    *total = saturating_add_u64(*total, elapsed);
    if (elapsed > *maximum) *maximum = elapsed;
    spinlock_release_irq(&runtime_timing_lock, flags);
}

uint64_t runtime_timing_begin(void) {
    return cpu_cycle_counter_read();
}

static void runtime_timing_finish_scheduler(uint64_t start_cycles) {
    runtime_timing_record(start_cycles, true);
}

void runtime_timing_record_syscall(uint64_t start_cycles) {
    runtime_timing_record(start_cycles, false);
}

int scheduler_runtime_timing_stats(runtime_timing_stats_t *stats_out) {
    if (stats_out == NULL) return -22;
    uint32_t flags = spinlock_acquire_irq(&runtime_timing_lock);
    *stats_out = runtime_timing_stats;
    stats_out->version = RUNTIME_TIMING_STATS_VERSION;
    stats_out->struct_size = sizeof(*stats_out);
    stats_out->cpu_frequency_hz = cpu_frequency;
    spinlock_release_irq(&runtime_timing_lock, flags);
    return stats_out->cpu_frequency_hz == 0U ? -5 : 0;
}

extern uint32_t _stack_guard_start;
extern uint32_t _stack_guard_end;
extern uint8_t _stack_start;
extern uint8_t _stack_end;

typedef struct {
    bool allocated;
    uintptr_t frames[STACK_SIZE / PAGE_SIZE];
} kernel_stack_slot_t;

static kernel_stack_slot_t kernel_stack_slots[KERNEL_STACK_SLOT_COUNT];
static uint32_t kernel_stack_high_water_peak;

static uint32_t kernel_stack_watermark_bytes(const uint32_t *stack) {
    const uint8_t *bytes = (const uint8_t *)stack;
    size_t untouched = 0U;
    for (size_t index = STACK_SIZE; index != 0U; --index) {
        if (bytes[index - 1U] != KERNEL_STACK_WATERMARK_BYTE) break;
        ++untouched;
    }
    return (uint32_t)(STACK_SIZE - untouched);
}

static void record_kernel_stack_watermark(const uint32_t *stack) {
    if (stack == NULL) return;
    uint32_t used = kernel_stack_watermark_bytes(stack);
    if (used > kernel_stack_high_water_peak)
        kernel_stack_high_water_peak = used;
}

static task_t *task_from_wait_node(wait_queue_node_t *node) {
    return (task_t*)((uint8_t*)node - offsetof(task_t, wait_node));
}

static int kernel_stack_slot_for(const uint32_t *stack) {
    uintptr_t address = (uintptr_t)stack;
    if (address < KERNEL_STACK_ARENA_BASE + PAGE_SIZE) return -1;
    uintptr_t offset = address - KERNEL_STACK_ARENA_BASE - PAGE_SIZE;
    if ((offset % KERNEL_STACK_SLOT_SIZE) != 0) return -1;
    size_t slot = offset / KERNEL_STACK_SLOT_SIZE;
    return slot < KERNEL_STACK_SLOT_COUNT ? (int)slot : -1;
}

uint32_t *scheduler_allocate_kernel_stack(void) {
    KASSERT_NOT_IRQ();
    for (size_t slot = 0; slot < KERNEL_STACK_SLOT_COUNT; ++slot) {
        if (kernel_stack_slots[slot].allocated) continue;
        uint32_t stack_base = KERNEL_STACK_ARENA_BASE +
                              (uint32_t)slot * KERNEL_STACK_SLOT_SIZE +
                              PAGE_SIZE;
        size_t mapped = 0;
        for (; mapped < STACK_SIZE / PAGE_SIZE; ++mapped) {
            uintptr_t frame = allocate_frame();
            if (frame == 0 ||
                map_page(paging_kernel_directory(),
                         stack_base + (uint32_t)mapped * PAGE_SIZE,
                         (uint32_t)frame, PAGE_RW) != 0) {
                if (frame != 0) free_frame(frame);
                while (mapped != 0) {
                    --mapped;
                    (void)unmap_kernel_page(
                        stack_base + (uint32_t)mapped * PAGE_SIZE, true);
                }
                return NULL;
            }
            kernel_stack_slots[slot].frames[mapped] = frame;
        }
        kernel_stack_slots[slot].allocated = true;
        uint32_t *stack = (uint32_t*)(uintptr_t)stack_base;
        memset(stack, KERNEL_STACK_WATERMARK_BYTE, STACK_SIZE);
        return stack;
    }
    return NULL;
}

static bool scheduler_kernel_stack_metadata_is_valid(const uint32_t *stack) {
    int slot = kernel_stack_slot_for(stack);
    if (slot < 0 || !kernel_stack_slots[slot].allocated) return false;
    for (size_t page = 0; page < STACK_SIZE / PAGE_SIZE; ++page) {
        if (kernel_stack_slots[slot].frames[page] == 0U) return false;
    }
    return true;
}

bool scheduler_kernel_stack_is_valid(const uint32_t *stack) {
    if (!scheduler_kernel_stack_metadata_is_valid(stack)) return false;
    uintptr_t base = (uintptr_t)stack;
    if (paging_kernel_page_present((uint32_t)(base - PAGE_SIZE)) ||
        paging_kernel_page_present((uint32_t)(base + STACK_SIZE))) {
        return false;
    }
    for (size_t page = 0; page < STACK_SIZE / PAGE_SIZE; ++page) {
        if (!paging_kernel_page_present((uint32_t)(base + page * PAGE_SIZE))) {
            return false;
        }
    }
    return true;
}

bool scheduler_kernel_context_stack_is_valid(void) {
    if (paging_is_enabled()) {
        return (uintptr_t)&_stack_guard_end -
                   (uintptr_t)&_stack_guard_start == PAGE_SIZE &&
               !paging_kernel_page_present(
                   (uint32_t)(uintptr_t)&_stack_guard_start);
    }
    const uint32_t *guard = &_stack_guard_start;
    const uint32_t *guard_end = &_stack_guard_end;
    if ((uintptr_t)guard_end - (uintptr_t)guard != PAGE_SIZE) return false;
    while (guard < guard_end) {
        if (*guard++ != KERNEL_STACK_GUARD) return false;
    }
    return true;
}

void scheduler_free_kernel_stack(uint32_t *stack) {
    if (stack == NULL) return;
    if (!scheduler_kernel_stack_is_valid(stack)) {
        panic("Kernel stack guard corrupted");
    }
    int slot = kernel_stack_slot_for(stack);
    memset(stack, 0xDD, STACK_SIZE);
    for (size_t page = 0; page < STACK_SIZE / PAGE_SIZE; ++page) {
        if (unmap_kernel_page((uint32_t)(uintptr_t)stack +
                              (uint32_t)page * PAGE_SIZE, true) != 0) {
            panic("Unable to release guarded kernel stack");
        }
        kernel_stack_slots[slot].frames[page] = 0;
    }
    kernel_stack_slots[slot].allocated = false;
}

static void validate_task_stack_or_panic(const task_t *task) {
    if (task == NULL || task->kernel_stack == NULL) return;
    uintptr_t low = (uintptr_t)task->kernel_stack;
    uintptr_t high = low + STACK_SIZE;
    if (!scheduler_kernel_stack_metadata_is_valid(task->kernel_stack) ||
        (task->context.esp != 0U &&
         ((uintptr_t)task->context.esp < low ||
          (uintptr_t)task->context.esp >= high))) {
        panic("Kernel stack guard or saved ESP corrupted");
    }
    record_kernel_stack_watermark(task->kernel_stack);
}

static void validate_running_task_stack_or_panic(const task_t *task) {
    validate_task_stack_or_panic(task);
    uintptr_t current_esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
    uintptr_t low = (uintptr_t)task->kernel_stack;
    if (current_esp < low || current_esp >= low + STACK_SIZE) {
        panic("Current ESP escaped the kernel task stack");
    }
}

/* Timer IRQ scheduling cannot acquire the page-table lock: a concurrent
 * kernel mapping may hold it while waiting for this CPU's TLB-shootdown ACK.
 * Full guard-page mapping validation remains mandatory at stack allocation
 * and release boundaries; a runtime overflow still faults on the unmapped
 * guard page without taking the mapping lock from a scheduler path. */
static void validate_running_task_stack_irq_or_panic(const task_t *task) {
    if (task == NULL || task->kernel_stack == NULL ||
        !scheduler_kernel_stack_metadata_is_valid(task->kernel_stack))
        panic("Kernel stack metadata corrupted in scheduler IRQ");
    uintptr_t low = (uintptr_t)task->kernel_stack;
    uintptr_t high = low + STACK_SIZE;
    uintptr_t current_esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
    if (current_esp < low || current_esp >= high ||
        (task->context.esp != 0U &&
         ((uintptr_t)task->context.esp < low ||
          (uintptr_t)task->context.esp >= high)))
        panic("Kernel stack bounds corrupted in scheduler IRQ");
    record_kernel_stack_watermark(task->kernel_stack);
}

static void validate_kernel_context_stack_or_panic(bool check_esp) {
    x86_cpu_local_t *local = scheduler_cpu_local();
    uintptr_t low;
    uintptr_t high;
    if (local->cpu_index == 0U) {
        if ((uintptr_t)&_stack_guard_end -
                (uintptr_t)&_stack_guard_start != PAGE_SIZE)
            panic("Static kernel stack layout corrupted");
        low = (uintptr_t)&_stack_start;
        high = (uintptr_t)&_stack_end;
    } else {
        low = local->kernel_idle_stack_low;
        high = local->kernel_idle_stack_high;
        if (low == 0U || high - low != STACK_SIZE ||
            !scheduler_kernel_stack_metadata_is_valid((const uint32_t *)low))
            panic("AP kernel idle stack metadata corrupted");
    }
    if (check_esp) {
        uintptr_t current_esp;
        __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
        if (current_esp < low || current_esp >= high)
            panic("Current ESP escaped the CPU kernel idle stack");
    }
}

static void validate_kernel_context_stack_irq_or_panic(void) {
    x86_cpu_local_t *local = scheduler_cpu_local();
    uintptr_t low;
    uintptr_t high;
    if (local->cpu_index == 0U) {
        if ((uintptr_t)&_stack_guard_end -
                (uintptr_t)&_stack_guard_start != PAGE_SIZE)
            panic("Static kernel stack layout corrupted in scheduler IRQ");
        low = (uintptr_t)&_stack_start;
        high = (uintptr_t)&_stack_end;
    } else {
        low = local->kernel_idle_stack_low;
        high = local->kernel_idle_stack_high;
        if (low == 0U || high - low != STACK_SIZE ||
            !scheduler_kernel_stack_metadata_is_valid(
                (const uint32_t *)low))
            panic("AP kernel stack metadata corrupted in scheduler IRQ");
    }
    uintptr_t current_esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
    if (current_esp < low || current_esp >= high)
        panic("Current ESP escaped the CPU stack in scheduler IRQ");
}

static void refresh_effective_classes_locked(void) {
    assert_task_table_locked();
    uint8_t base[MAX_TASKS] = {0};
    uint8_t effective[MAX_TASKS] = {0};
    int8_t owners[MAX_TASKS];
    for (int index = 0; index < num_tasks; ++index) {
        base[index] = tasks[index].scheduling_class;
        owners[index] = -1;
        int owner = tasks[index].blocked_owner_task;
        if ((tasks[index].status == TASK_WAITING ||
             tasks[index].status == TASK_SLEEPING) &&
            owner >= 0 && owner < num_tasks &&
            tasks[owner].process_generation ==
                tasks[index].blocked_owner_generation &&
            (tasks[owner].status == TASK_READY ||
             tasks[owner].status == TASK_RUNNING)) {
            owners[index] = (int8_t)owner;
        }
    }
    scheduler_policy_inherit(effective, base, owners, num_tasks);
    for (int index = 0; index < num_tasks; ++index) {
        if (tasks[index].effective_scheduling_class != effective[index]) {
            tasks[index].effective_scheduling_class = effective[index];
            tasks[index].budget_remaining = scheduler_policy_budget(
                effective[index]);
        }
    }
}

static void account_current_runtime_locked(uint64_t now_ms) {
    assert_task_table_locked();
    uint32_t cpu = scheduler_cpu_policy_index_locked();
    uint8_t charged_class = current_task >= 0 && current_task < num_tasks
        ? dispatched_classes[cpu]
        : SCHEDULER_CLASS_NONE;
    /* Effective classes may already have changed on another CPU. Bill the
     * class actually dispatched, including its excess background execution. */
    KASSERT(current_task < 0 || charged_class < SCHEDULER_CLASS_COUNT);
    if (scheduler_policy_window_charge(
            &cpu_windows[cpu], charged_class, now_ms)) {
        for (int index = 0; index < num_tasks; ++index) {
            tasks[index].budget_remaining = scheduler_policy_budget(
                tasks[index].effective_scheduling_class);
        }
    }
    refresh_effective_classes_locked();
}

static int find_next_runnable(int after, uint64_t now_ms) {
    assert_task_table_locked();
    (void)after;
    if (num_tasks == 0) {
        return -1;
    }
    account_current_runtime_locked(now_ms);
    scheduler_candidate_t candidates[MAX_TASKS] = {0};
    int32_t cpu = (int32_t)scheduler_cpu_local()->cpu_index;
    uint32_t policy_cpu = scheduler_cpu_policy_index_locked();
    uint32_t cpu_bit = 1U << (uint32_t)cpu;
    uint32_t runnable_mask = 0U;
    for (int index = 0; index < num_tasks; ++index) {
        bool ready_unowned = tasks[index].status == TASK_READY &&
            tasks[index].running_cpu == TASK_CPU_NONE;
        bool running_here = tasks[index].status == TASK_RUNNING &&
            tasks[index].running_cpu == cpu;
        bool runnable =
            (ready_unowned || running_here) &&
            (tasks[index].cpu_affinity_mask & cpu_bit) != 0U;
        if (runnable) runnable_mask |= 1U << (uint32_t)index;
        candidates[index].runnable = runnable &&
            scheduler_policy_class_allowed(&cpu_windows[policy_cpu],
                tasks[index].effective_scheduling_class);
        candidates[index].scheduling_class =
            tasks[index].effective_scheduling_class;
        candidates[index].budget_remaining = tasks[index].budget_remaining;
    }
    int selected = scheduler_policy_select_cycle(
        candidates, num_tasks, scheduling_class_cursors[policy_cpu],
        &scheduling_class_cycle_cursors[policy_cpu]);
    if (selected < 0) {
        /* A second fixed-capacity pass can only consume otherwise idle time.
         * Funded candidates always win; affinity/ownership never change. */
        for (int index = 0; index < num_tasks; ++index) {
            candidates[index].runnable =
                (runnable_mask & (1U << (uint32_t)index)) != 0U &&
                scheduler_policy_background_allowed(&cpu_windows[policy_cpu],
                    candidates[index].scheduling_class);
        }
        selected = scheduler_policy_select_cycle(
            candidates, num_tasks, scheduling_class_cursors[policy_cpu],
            &scheduling_class_cycle_cursors[policy_cpu]);
    }
    for (int index = 0; index < num_tasks; ++index)
        tasks[index].budget_remaining = candidates[index].budget_remaining;
    return selected;
}

static bool claim_task_for_current_cpu(int task_id) {
    KASSERT_IRQ_DISABLED();
    if (task_id < 0 || task_id >= num_tasks) return false;
    task_t *task = &tasks[task_id];
    int32_t cpu = (int32_t)scheduler_cpu_local()->cpu_index;
    if ((task->cpu_affinity_mask & (1U << (uint32_t)cpu)) == 0U)
        return false;
    if (task->status == TASK_RUNNING) return task->running_cpu == cpu;
    if (task->status != TASK_READY ||
        !__sync_bool_compare_and_swap(&task->running_cpu,
                                      TASK_CPU_NONE, cpu)) return false;
    __sync_synchronize();
    if (task->status != TASK_READY) {
        bool released = __sync_bool_compare_and_swap(&task->running_cpu,
                                                     cpu, TASK_CPU_NONE);
        KASSERT(released);
        return false;
    }
    task->status = TASK_RUNNING;
    __sync_synchronize();
    return true;
}

static int claim_next_runnable(int after, uint64_t now_ms) {
    for (uint32_t attempt = 0U; attempt < MAX_TASKS; ++attempt) {
        int next = find_next_runnable(after, now_ms);
        if (next < 0) return -1;
        if (claim_task_for_current_cpu(next)) {
            dispatched_classes[scheduler_cpu_policy_index_locked()] =
                tasks[next].effective_scheduling_class;
            return next;
        }
    }
    return -1;
}

static void prepare_task_handoff(int task_id, uint32_t action) {
    KASSERT_IRQ_DISABLED();
    KASSERT(task_id >= 0 && task_id < num_tasks);
    KASSERT(action == SCHEDULER_HANDOFF_READY ||
            action == SCHEDULER_HANDOFF_RELEASE);
    KASSERT(handoff_task == -1);
    KASSERT(handoff_action == SCHEDULER_HANDOFF_NONE);
    task_t *task = &tasks[task_id];
    int32_t cpu = (int32_t)scheduler_cpu_local()->cpu_index;
    KASSERT(task->running_cpu == cpu);
    if (action == SCHEDULER_HANDOFF_READY) task->status = TASK_HANDOFF;
    handoff_task = task_id;
    handoff_action = action;
    __sync_synchronize();
}

static void finish_task_handoff(void) {
    KASSERT_IRQ_DISABLED();
    int task_id = handoff_task;
    uint32_t action = handoff_action;
    if (task_id < 0) {
        KASSERT(action == SCHEDULER_HANDOFF_NONE);
        return;
    }
    KASSERT(task_id < num_tasks);
    KASSERT(action == SCHEDULER_HANDOFF_READY ||
            action == SCHEDULER_HANDOFF_RELEASE);
    task_t *task = &tasks[task_id];
    int32_t cpu = (int32_t)scheduler_cpu_local()->cpu_index;
    KASSERT(task->running_cpu == cpu);
    if (action == SCHEDULER_HANDOFF_READY) {
        KASSERT(task->status == TASK_HANDOFF);
        task->status = TASK_READY;
    }
    __sync_synchronize();
    bool released = __sync_bool_compare_and_swap(&task->running_cpu,
                                                 cpu, TASK_CPU_NONE);
    KASSERT(released);
    handoff_action = SCHEDULER_HANDOFF_NONE;
    handoff_task = -1;
    __sync_synchronize();
}

static uint32_t active_task_count_locked(void) {
    assert_task_table_locked();
    uint32_t active = 0U;
    for (int index = 0; index < num_tasks; ++index) {
        if (tasks[index].status != TASK_FINISHED &&
            tasks[index].status != TASK_REAPING) ++active;
    }
    return active;
}

static void note_task_capacity_rejection_locked(void) {
    assert_task_table_locked();
    if (task_capacity_rejections != UINT32_MAX) ++task_capacity_rejections;
}

static void task_trampoline(void) __attribute__((noreturn));
static void task_trampoline(void) {
    finish_task_handoff();
    int index = current_task;
    if (index < 0 || index >= num_tasks || tasks[index].context.eip == 0) {
        scheduler_kill_current();
    }

    if (tasks[index].user_mode) {
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
        if (tasks[index].process != NULL &&
            tasks[index].process->domain_profile.kind == PROCESS_DOMAIN_DRIVER &&
            scheduler_cpu_local()->cpu_index != 0U)
            printf("DRIVER_DOMAIN AP_EXEC cpu=%u\n",
                   scheduler_cpu_local()->cpu_index);
#endif
        enter_user_mode(tasks[index].user_entry, tasks[index].user_stack);
    }

    irq_enable();
    void (*entry_point)(void) = (void (*)(void))tasks[index].context.eip;
    entry_point();
    task_exit();
}

static void release_task_resources(task_t *task) {
    KASSERT(task->running_cpu == TASK_CPU_NONE);
    KASSERT(task->process == NULL || process_table_lock_is_owned());
    int task_id = (int)(task - tasks);
    wait_queue_cancel_locked(task);
    if (task->process != NULL &&
        task->process->generation == task->process_generation &&
        task->process->task_id == task_id) {
        /* A zombie keeps its PID and exit status until wait(), but it must not
         * retain a numerical task slot after that slot becomes reusable. */
        task->process->task_id = -1;
    }
    task->reap_page_directory =
        task->page_directory != paging_kernel_directory()
            ? task->page_directory : NULL;
    task->reap_kernel_stack = task->kernel_stack;
    task->reap_page_cursor = 0U;
    task->reap_busy = false;
    task->page_directory = NULL;
    task->kernel_stack = NULL;
    task->process = NULL;
    task->process_generation = 0U;
    task->status = TASK_REAPING;
}

int scheduler_reap_finished_task_locked(int task_id, const Process *owner) {
    KASSERT_IRQ_DISABLED();
    KASSERT(process_table_lock_is_owned());
    spinlock_acquire(&task_table_lock);
    if (irq_enabled() || task_id < 0 || task_id >= num_tasks ||
        task_id == current_task || tasks[task_id].status != TASK_FINISHED ||
        tasks[task_id].running_cpu != TASK_CPU_NONE ||
        (owner != NULL && (tasks[task_id].process != owner ||
                           tasks[task_id].process_generation !=
                               owner->generation))) {
        spinlock_release(&task_table_lock);
        return -1;
    }
    release_task_resources(&tasks[task_id]);
    spinlock_release(&task_table_lock);
    return 0;
}

size_t scheduler_reap_finished_tasks(void) {
    size_t reaped = 0;
    scheduler_preempt_disable();

    uint32_t process_flags = process_table_lock_irqsave();
    spinlock_acquire(&task_table_lock);
    for (int task_id = 0; task_id < num_tasks; ++task_id) {
        task_t *task = &tasks[task_id];
        bool owns_resources = task->kernel_stack != NULL ||
                              task->page_directory != NULL ||
                              task->process != NULL ||
                              task->wait_node.queue != NULL;
        if (task_id != current_task && task->status == TASK_FINISHED &&
            task->running_cpu == TASK_CPU_NONE &&
            owns_resources) {
            release_task_resources(task);
        }
    }
    spinlock_release(&task_table_lock);
    process_table_unlock_irqrestore(process_flags);

    /* Each bounded step retains its directory/cursor in TASK_REAPING. No
     * process can inherit it; a killed reaper leaves progress for another one.
     * Suppress preemption only for one step, not a multi-hundred-MiB teardown. */
    for (int task_id = 0; task_id < num_tasks; ++task_id) {
      uint32_t expected_generation = 0U;
      for (uint32_t step = 0U;
           step < (USER_PAGE_END-USER_PAGE_START)*PAGE_TABLE_ENTRIES/64U+1U; ++step) {
        page_directory_t *page_directory = NULL;
        uint32_t *kernel_stack = NULL;

        uint32_t flags = task_table_lock_irqsave();
        task_t *task = &tasks[task_id];
        if (task->status == TASK_REAPING && !task->reap_busy &&
            (expected_generation == 0U || task->task_generation == expected_generation) &&
            (task->reap_page_directory != NULL ||
             task->reap_kernel_stack != NULL)) {
            KASSERT(task->task_generation != 0U);
            expected_generation = task->task_generation;
            task->reap_busy = true;
            page_directory = task->reap_page_directory;
            kernel_stack = task->reap_kernel_stack;
        }
        task_table_unlock_irqrestore(flags);

        if (page_directory == NULL && kernel_stack == NULL) break;
        bool complete = page_directory == NULL ||
            free_page_directory_step(page_directory, &task->reap_page_cursor);
        if (complete && kernel_stack != NULL) scheduler_free_kernel_stack(kernel_stack);

        flags = task_table_lock_irqsave();
        task = &tasks[task_id];
        KASSERT(task->status == TASK_REAPING && task->reap_busy);
        task->reap_busy = false;
        if (complete) {
            memset(task, 0, sizeof(*task));
            task->status = TASK_FINISHED;
            task->running_cpu = TASK_CPU_NONE;
            ++reaped;
        }
        task_table_unlock_irqrestore(flags);
        if (complete) break;
        scheduler_preempt_enable();
        bool can_continue = scheduler_can_sleep();
        if (can_continue) (void)scheduler_yield();
        scheduler_preempt_disable();
        if (!can_continue) break;
      }
    }

    scheduler_preempt_enable();
    return reaped;
}

static int create_task_with_affinity(void (*entry_point)(void),
                                     uint32_t *stack, Process *process,
                                     uint32_t cpu_affinity_mask) {
    if (!entry_point || !stack || !scheduler_kernel_stack_is_valid(stack)) {
        return -1;
    }
    if (cpu_affinity_mask == 0U ||
        (cpu_affinity_mask & ~((1U << X86_CPU_LOCAL_MAX) - 1U)) != 0U)
        return -1;
    for (uint32_t cpu = 0U; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        if ((cpu_affinity_mask & (1U << cpu)) == 0U) continue;
        x86_cpu_local_t *local = x86_cpu_local_by_index(cpu);
        if (local == NULL || local->online == 0U ||
            !x86_fpu_cpu_ready(cpu)) return -1;
    }

    bool process_locked = process != NULL;
    uint32_t process_flags = 0U;
    if (process_locked) process_flags = process_table_lock_irqsave();
    uint32_t flags = task_table_lock_irqsave();
    int task_id = -1;
    for (int i = 0; i < num_tasks; ++i) {
        if (tasks[i].status == TASK_FINISHED && i != current_task &&
            tasks[i].kernel_stack == NULL &&
            tasks[i].reap_kernel_stack == NULL &&
            tasks[i].reap_page_directory == NULL) {
            task_id = i;
            break;
        }
    }

    if (task_id < 0) {
        if (num_tasks >= MAX_TASKS) {
            note_task_capacity_rejection_locked();
            task_table_unlock_irqrestore(flags);
            if (process_locked)
                process_table_unlock_irqrestore(process_flags);
            printf("Error: Maximum number of tasks reached!\n");
            return -1;
        }
        task_id = num_tasks;
    }

    task_t *task = &tasks[task_id];
    memset(task, 0, sizeof(*task));
    KASSERT(next_task_generation != UINT32_MAX);
    KASSERT(x86_fpu_state_reset(task->context.fpu_state));
    ++next_task_generation;
    task->task_generation = next_task_generation;
    task->status = TASK_FINISHED;
    task->kernel_stack = stack;
    task->context.eip = (uint32_t)entry_point;
    task->is_started = 1;
    task->process = process;
    task->process_generation = process != NULL ? process->generation : 0U;
    task->page_directory = paging_kernel_directory();
    task->scheduling_class = process == NULL ? SCHEDULER_CLASS_SAFETY :
                                               SCHEDULER_CLASS_AMBIENT;
    task->effective_scheduling_class = task->scheduling_class;
    task->budget_remaining = scheduler_policy_budget(
        task->scheduling_class);
    task->blocked_owner_task = -1;
    task->running_cpu = TASK_CPU_NONE;
    task->cpu_affinity_mask = cpu_affinity_mask;

    uintptr_t top = ((uintptr_t)stack + STACK_SIZE) & ~(uintptr_t)0x0F;
    uint32_t *initial_stack = (uint32_t*)top;
    *(--initial_stack) = (uint32_t)task_exit;       // Fallback return address
    *(--initial_stack) = (uint32_t)task_trampoline; // First RET target
    task->context.esp = (uint32_t)initial_stack;
    if (process != NULL) {
        process->task_id = task_id;
    }
    task->status = TASK_READY;
    if (task_id == num_tasks) {
        num_tasks++;
    }
    uint32_t active = active_task_count_locked();
    if (active > peak_active_tasks) peak_active_tasks = active;

    task_table_unlock_irqrestore(flags);
    if (process_locked) process_table_unlock_irqrestore(process_flags);
    return task_id;
}

static bool scheduler_affinity_online(uint32_t cpu_affinity_mask) {
    if (cpu_affinity_mask == 0U ||
        (cpu_affinity_mask & ~((1U << X86_CPU_LOCAL_MAX) - 1U)) != 0U)
        return false;
    for (uint32_t cpu = 0U; cpu < X86_CPU_LOCAL_MAX; ++cpu) {
        if ((cpu_affinity_mask & (1U << cpu)) == 0U) continue;
        x86_cpu_local_t *local = x86_cpu_local_by_index(cpu);
        if (local == NULL || local->online == 0U) return false;
    }
    return true;
}

static size_t available_task_slots_locked(void) {
    assert_task_table_locked();
    size_t available = MAX_TASKS - num_tasks;
    for (int index = 0; index < num_tasks; ++index) {
        if (index != current_task && tasks[index].status == TASK_FINISHED &&
            tasks[index].running_cpu == TASK_CPU_NONE &&
            tasks[index].kernel_stack == NULL &&
            tasks[index].reap_kernel_stack == NULL &&
            tasks[index].reap_page_directory == NULL) ++available;
    }
    return available;
}

static int create_user_task_admitted(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process, bool supervised,
    bool prepared, uint32_t cpu_affinity_mask) {
    if (entry_point < USER_BASE || entry_point >= USER_TOP ||
        user_stack <= USER_BASE || user_stack > USER_TOP || !kernel_stack ||
        !page_directory) return -1;
    scheduler_preempt_disable();
    uint32_t admission_flags = task_table_lock_irqsave();
    size_t available = available_task_slots_locked();
    bool admitted = available != 0U &&
        (supervised || available > SUPERVISED_TASK_RESERVE);
    task_table_unlock_irqrestore(admission_flags);
    if (!admitted) {
        uint32_t flags = task_table_lock_irqsave();
        note_task_capacity_rejection_locked();
        task_table_unlock_irqrestore(flags);
        scheduler_preempt_enable();
        return -1;
    }
    int task_id = create_task_with_affinity(
        (void (*)(void))(uintptr_t)entry_point, kernel_stack, process,
        cpu_affinity_mask);
    if (task_id < 0) {
        scheduler_preempt_enable();
        return -1;
    }
    uint32_t flags = task_table_lock_irqsave();
    task_t *task = &tasks[task_id];
    task->page_directory = page_directory;
    task->user_entry = entry_point;
    task->user_stack = user_stack;
    task->user_mode = true;
    task->scheduling_class = supervised ? SCHEDULER_CLASS_SERVICE :
                                          SCHEDULER_CLASS_AMBIENT;
    task->effective_scheduling_class = task->scheduling_class;
    task->budget_remaining = scheduler_policy_budget(
        task->scheduling_class);
    if (prepared) task->status = TASK_PREPARED;
    task_table_unlock_irqrestore(flags);
    scheduler_preempt_enable();
    return task_id;
}

int create_task(void (*entry_point)(void), uint32_t *stack, Process *process) {
    return create_task_with_affinity(entry_point, stack, process,
                                     TASK_CPU_MASK_BSP);
}

int create_affined_kernel_task(void (*entry_point)(void), uint32_t *stack,
                               uint32_t cpu_affinity_mask) {
    return create_task_with_affinity(entry_point, stack, NULL,
                                     cpu_affinity_mask);
}

int create_user_task(uint32_t entry_point, uint32_t user_stack,
                     uint32_t *kernel_stack, page_directory_t *page_directory,
                     Process *process) {
    return create_user_task_admitted(entry_point, user_stack, kernel_stack,
                                     page_directory, process, false, false,
                                     TASK_CPU_MASK_BSP);
}

int create_supervised_user_task(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process) {
    return create_user_task_admitted(entry_point, user_stack, kernel_stack,
                                     page_directory, process, true, false,
                                     TASK_CPU_MASK_BSP);
}

int create_affined_supervised_user_task(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process,
    uint32_t cpu_affinity_mask) {
    return create_user_task_admitted(entry_point, user_stack, kernel_stack,
                                     page_directory, process, true, false,
                                     cpu_affinity_mask);
}

int create_prepared_supervised_user_task(
    uint32_t entry_point, uint32_t user_stack, uint32_t *kernel_stack,
    page_directory_t *page_directory, Process *process) {
    return create_user_task_admitted(entry_point, user_stack, kernel_stack,
                                     page_directory, process, true, true,
                                     TASK_CPU_MASK_BSP);
}

int scheduler_start_prepared_user_task_locked(
        int task_id, const Process *owner, uint32_t process_generation) {
    KASSERT_IRQ_DISABLED();
    KASSERT(process_table_lock_is_owned());
    if (task_id < 0 || owner == NULL || process_generation == 0U)
        return -22;
    spinlock_acquire(&task_table_lock);
    int result = -3;
    if (task_id < num_tasks && tasks[task_id].process == owner &&
        tasks[task_id].process_generation == process_generation &&
        tasks[task_id].status == TASK_PREPARED &&
        tasks[task_id].running_cpu == TASK_CPU_NONE) {
        tasks[task_id].status = TASK_READY;
        result = 0;
    }
    spinlock_release(&task_table_lock);
    return result;
}

int scheduler_set_task_affinity_locked(
        int task_id, const Process *owner, uint32_t process_generation,
        uint32_t cpu_affinity_mask) {
    KASSERT_IRQ_DISABLED();
    KASSERT(process_table_lock_is_owned());
    if (task_id < 0 || owner == NULL || process_generation == 0U ||
        !scheduler_affinity_online(cpu_affinity_mask)) return -22;
    spinlock_acquire(&task_table_lock);
    int result = -3;
    if (task_id < num_tasks && tasks[task_id].process == owner &&
        tasks[task_id].process_generation == process_generation &&
        tasks[task_id].status != TASK_FINISHED) {
        tasks[task_id].cpu_affinity_mask = cpu_affinity_mask;
        result = 0;
    }
    spinlock_release(&task_table_lock);
    return result;
}

static void activate_task_address_space(int task_index) {
    if (task_index >= 0 && task_index < num_tasks) {
        task_t *task = &tasks[task_index];
        validate_task_stack_or_panic(task);
        switch_page_directory(task->page_directory);
        tss_set_kernel_stack((uint32_t)(uintptr_t)task->kernel_stack +
                             STACK_SIZE);
    } else {
        switch_page_directory(paging_kernel_directory());
    }
}

static bool schedule_blocked_current_locked(int blocked, uint64_t now_ms) {
    validate_running_task_stack_or_panic(&tasks[blocked]);
    assert_task_table_locked();
    int next = claim_next_runnable(blocked, now_ms);
    if (next >= 0) {
        prepare_task_handoff(blocked, SCHEDULER_HANDOFF_RELEASE);
        current_task = next;
        spinlock_release(&task_table_lock);
        activate_task_address_space(next);
        swtch(&tasks[blocked].context, &tasks[next].context);
        finish_task_handoff();
        return true;
    }

    if (!kernel_context_saved) {
        spinlock_release(&task_table_lock);
        return false;
    }
    validate_kernel_context_stack_or_panic(false);
    prepare_task_handoff(blocked, SCHEDULER_HANDOFF_RELEASE);
    current_task = -1;
    spinlock_release(&task_table_lock);
    activate_task_address_space(-1);
    swtch(&tasks[blocked].context, &kernel_context);
    finish_task_handoff();
    return true;
}

void wait_queue_cancel_locked(task_t *task) {
    KASSERT_IRQ_DISABLED();
    assert_task_table_locked();
    if (task == NULL) return;
    if (task->wait_node.queue != NULL)
        (void)wait_queue_remove_locked(task->wait_node.queue,
                                       &task->wait_node);
    task->wait_node.key = 0;
    task->wait_deadline_ms = 0;
    task->blocked_owner_task = -1;
    task->blocked_owner_generation = 0U;
}

bool scheduler_set_wait_owner_locked(int pid, uint32_t generation) {
    KASSERT_IRQ_DISABLED();
    if (irq_enabled() || pid <= 0 || generation == 0U) return false;
    spinlock_acquire(&task_table_lock);
    if (current_task < 0 || current_task >= num_tasks) {
        spinlock_release(&task_table_lock);
        return false;
    }
    for (int owner = 0; owner < num_tasks; ++owner) {
        task_t *candidate = &tasks[owner];
        if (owner != current_task && candidate->process != NULL &&
            candidate->process->pid == pid &&
            candidate->process_generation == generation &&
            candidate->process->generation == generation &&
            (candidate->status == TASK_READY ||
             candidate->status == TASK_RUNNING)) {
            tasks[current_task].blocked_owner_task = (int8_t)owner;
            tasks[current_task].blocked_owner_generation = generation;
            spinlock_release(&task_table_lock);
            return true;
        }
    }
    spinlock_release(&task_table_lock);
    return false;
}

void scheduler_clear_wait_owner_locked(void) {
    uint32_t flags = irq_save();
    spinlock_acquire(&task_table_lock);
    if (current_task >= 0 && current_task < num_tasks) {
        tasks[current_task].blocked_owner_task = -1;
        tasks[current_task].blocked_owner_generation = 0U;
    }
    spinlock_release(&task_table_lock);
    irq_restore(flags);
}

static int wait_queue_block_until_task_locked(wait_queue_t *queue,
                                              task_block_kind_t kind,
                                              uint64_t deadline_ms,
                                              uint64_t now_ms) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    assert_task_table_locked();
    if (queue == NULL || irq_enabled() || preempt_disable_count != 0 ||
        current_task < 0 || current_task >= num_tasks ||
        (kind != TASK_BLOCK_WAITING && kind != TASK_BLOCK_SLEEPING)) {
        spinlock_release(&task_table_lock);
        return -1;
    }

    int blocked = current_task;
    task_t *task = &tasks[blocked];
    if (task->status != TASK_RUNNING || task->wait_node.queue != NULL ||
        !wait_queue_push_locked(queue, &task->wait_node)) {
        spinlock_release(&task_table_lock);
        return -1;
    }
    task->wait_deadline_ms = deadline_ms;
    task->wait_result = 0;
    task->status = kind == TASK_BLOCK_SLEEPING
        ? TASK_SLEEPING : TASK_WAITING;

    if (!schedule_blocked_current_locked(blocked, now_ms)) {
        spinlock_acquire(&task_table_lock);
        wait_queue_cancel_locked(task);
        task->status = TASK_RUNNING;
        current_task = blocked;
        spinlock_release(&task_table_lock);
        return -1;
    }
    return task->wait_result;
}

int wait_queue_block_until_locked(wait_queue_t *queue,
                                  task_block_kind_t kind,
                                  uint64_t deadline_ms) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    uint64_t now_ms = pit_monotonic_ms();
    spinlock_acquire(&task_table_lock);
    return wait_queue_block_until_task_locked(queue, kind, deadline_ms,
                                              now_ms);
}

int wait_queue_block_until_spinlocked(wait_queue_t *queue,
                                      task_block_kind_t kind,
                                      uint64_t deadline_ms,
                                      spinlock_t *condition_lock,
                                      uint32_t irq_flags) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    KASSERT(condition_lock != NULL);
    KASSERT(spinlock_is_owned_by_current(condition_lock));
    uint64_t now_ms = pit_monotonic_ms();
    spinlock_acquire(&task_table_lock);
    spinlock_release(condition_lock);
    int result = wait_queue_block_until_task_locked(queue, kind, deadline_ms,
                                                    now_ms);
    irq_restore(irq_flags);
    return result;
}

int wait_queue_block_locked(wait_queue_t *queue, task_block_kind_t kind) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    return wait_queue_block_until_locked(queue, kind, UINT64_MAX);
}

static bool wait_queue_wake_one_task_locked(wait_queue_t *queue,
                                            uint32_t *reschedule_mask) {
    KASSERT_IRQ_DISABLED();
    assert_task_table_locked();
    if (queue == NULL || irq_enabled()) return false;
    for (;;) {
        wait_queue_node_t *node = wait_queue_pop_locked(queue);
        if (node == NULL) return false;
        task_t *task = task_from_wait_node(node);
        node->key = 0;
        if (task->status == TASK_WAITING || task->status == TASK_SLEEPING) {
            task->wait_deadline_ms = 0;
            task->wait_result = 0;
            task->blocked_owner_task = -1;
            task->blocked_owner_generation = 0U;
            task->status = TASK_READY;
            if (reschedule_mask != NULL)
                *reschedule_mask |= task->cpu_affinity_mask;
            return true;
        }
    }
}

bool wait_queue_wake_one_locked(wait_queue_t *queue) {
    KASSERT_IRQ_DISABLED();
    uint32_t reschedule_mask = 0U;
    spinlock_acquire(&task_table_lock);
    bool woke = wait_queue_wake_one_task_locked(queue, &reschedule_mask);
    spinlock_release(&task_table_lock);
    if (woke)
        (void)x86_smp_request_reschedule(reschedule_mask);
    return woke;
}

static size_t wait_queue_wake_all_task_locked(wait_queue_t *queue,
                                              uint32_t *reschedule_mask) {
    assert_task_table_locked();
    size_t count = 0U;
    while (wait_queue_wake_one_task_locked(queue, reschedule_mask)) ++count;
    return count;
}

void scheduler_wake_expired_waiters_locked(uint64_t now_ms) {
    KASSERT_IRQ_DISABLED();
    if (irq_enabled()) return;
    spinlock_acquire(&task_table_lock);
    for (size_t index = 0; index < MAX_TASKS; ++index) {
        task_t *task = &tasks[index];
        if (task->status != TASK_WAITING || task->wait_node.queue == NULL ||
            task->wait_deadline_ms == UINT64_MAX ||
            task->wait_deadline_ms > now_ms) continue;
        if (wait_queue_remove_locked(task->wait_node.queue,
                                     &task->wait_node)) {
            task->wait_deadline_ms = 0;
            task->wait_result = -110;
            task->blocked_owner_task = -1;
            task->blocked_owner_generation = 0U;
            task->status = TASK_READY;
        }
    }
    spinlock_release(&task_table_lock);
}

size_t wait_queue_wake_all_locked(wait_queue_t *queue) {
    KASSERT_IRQ_DISABLED();
    uint32_t reschedule_mask = 0U;
    spinlock_acquire(&task_table_lock);
    size_t count = wait_queue_wake_all_task_locked(queue, &reschedule_mask);
    spinlock_release(&task_table_lock);
    if (count != 0U)
        (void)x86_smp_request_reschedule(reschedule_mask);
    return count;
}

void scheduler_wake_expired_sleepers_locked(uint64_t now_ms) {
    KASSERT_IRQ_DISABLED();
    if (irq_enabled()) return;
    spinlock_acquire(&task_table_lock);
    while (sleep_waiters.head != NULL &&
           sleep_waiters.head->key <= now_ms) {
        (void)wait_queue_wake_one_task_locked(&sleep_waiters, NULL);
    }
    spinlock_release(&task_table_lock);
}

int scheduler_sleep_ms(uint32_t milliseconds) {
    KASSERT_CAN_SLEEP();
    if (milliseconds == 0) return 0;
    uint32_t flags = irq_save();
    if (preempt_disable_count != 0 || irq_in_context() || current_task < 0 ||
        current_task >= num_tasks) {
        irq_restore(flags);
        return -1;
    }

    uint64_t now = pit_monotonic_ms();
    uint64_t deadline = now + (uint64_t)milliseconds;
    if (deadline < now) deadline = UINT64_MAX;

    spinlock_acquire(&task_table_lock);
    int blocked = current_task;
    task_t *task = &tasks[blocked];
    if (task->status != TASK_RUNNING || task->wait_node.queue != NULL ||
        !wait_queue_insert_ordered_locked(&sleep_waiters, &task->wait_node,
                                          deadline)) {
        spinlock_release(&task_table_lock);
        irq_restore(flags);
        return -1;
    }
    task->status = TASK_SLEEPING;
    if (!schedule_blocked_current_locked(blocked, now)) {
        spinlock_acquire(&task_table_lock);
        wait_queue_cancel_locked(task);
        task->status = TASK_RUNNING;
        current_task = blocked;
        spinlock_release(&task_table_lock);
        irq_restore(flags);
        return -1;
    }
    irq_restore(flags);
    return 0;
}

int scheduler_yield(void) {
    KASSERT_CAN_SLEEP();
    uint32_t flags = irq_save();
    if (preempt_disable_count != 0) {
        irq_restore(flags);
        return -1;
    }
    int previous = current_task;
    if (previous < 0 || previous >= num_tasks) {
        irq_restore(flags);
        return 0;
    }
    validate_running_task_stack_or_panic(&tasks[previous]);
    uint64_t now_ms = pit_monotonic_ms();

    spinlock_acquire(&task_table_lock);
    /* Keep the current CPU-owned task eligible. Marking it READY here hides
     * it from both unowned-ready and running-here selection on a lone yield.
     * The handoff below publishes READY only when execution really moves. */
    int next = claim_next_runnable(previous, now_ms);
    uint32_t policy_cpu = scheduler_cpu_policy_index_locked();
    bool previous_allowed = scheduler_policy_class_allowed(
        &cpu_windows[policy_cpu],
        tasks[previous].effective_scheduling_class);
    if (next < 0 && !previous_allowed &&
        kernel_context_saved) {
        prepare_task_handoff(previous, SCHEDULER_HANDOFF_READY);
        current_task = -1;
        spinlock_release(&task_table_lock);
        activate_task_address_space(-1);
        swtch(&tasks[previous].context, &kernel_context);
        finish_task_handoff();
        irq_restore(flags);
        return 0;
    }
    if (next < 0 || next == previous) {
        tasks[previous].status = TASK_RUNNING;
        spinlock_release(&task_table_lock);
        irq_restore(flags);
        return 0;
    }

    prepare_task_handoff(previous, SCHEDULER_HANDOFF_READY);
    current_task = next;
    spinlock_release(&task_table_lock);
    activate_task_address_space(next);
    swtch(&tasks[previous].context, &tasks[next].context);
    finish_task_handoff();
    irq_restore(flags);
    return 0;
}

void scheduler_set_apic_timer_active(bool active) {
    uint32_t flags = irq_save();
    apic_timer_active = active;
    pit_scheduler_ticks = 0;
    irq_restore(flags);
}

bool scheduler_current_task_identity(int *task_id_out,
                                     uint32_t *generation_out) {
    if (task_id_out == NULL || generation_out == NULL) return false;
    uint32_t flags = task_table_lock_irqsave();
    bool valid = current_task >= 0 && current_task < num_tasks &&
                 tasks[current_task].task_generation != 0U;
    *task_id_out = valid ? current_task : -1;
    *generation_out = valid ? tasks[current_task].task_generation : 0U;
    task_table_unlock_irqrestore(flags);
    return valid;
}

bool scheduler_current_task_identity_pinned(int *task_id_out,
                                            uint32_t *generation_out) {
    if (task_id_out == NULL || generation_out == NULL ||
        !scheduler_preempt_is_disabled()) return false;
    uint32_t flags = irq_save();
    x86_cpu_local_t *local = scheduler_cpu_local();
    int task_id = local->scheduler_current_task;
    bool valid = task_id >= 0 && task_id < num_tasks &&
                 tasks[task_id].status == TASK_RUNNING &&
                 tasks[task_id].running_cpu == (int32_t)local->cpu_index &&
                 tasks[task_id].task_generation != 0U;
    *task_id_out = valid ? task_id : -1;
    *generation_out = valid ? tasks[task_id].task_generation : 0U;
    irq_restore(flags);
    return valid;
}

int scheduler_mutex_owner_register(int task_id, uint32_t task_generation,
                                   void *mutex) {
    if (task_id < 0 || task_generation == 0U || mutex == NULL) return -22;
    uint32_t flags = task_table_lock_irqsave();
    int result = -116;
    if (task_id < num_tasks &&
        tasks[task_id].task_generation == task_generation &&
        tasks[task_id].status != TASK_FINISHED &&
        tasks[task_id].status != TASK_REAPING) {
        task_t *task = &tasks[task_id];
        result = -28;
        for (uint32_t index = 0U; index < task->held_mutex_count; ++index)
            KASSERT(task->held_mutexes[index] != mutex);
        if (task->held_mutex_count < SCHEDULER_HELD_MUTEX_CAPACITY) {
            task->held_mutexes[task->held_mutex_count++] = mutex;
            result = 0;
        }
    }
    task_table_unlock_irqrestore(flags);
    return result;
}

int scheduler_mutex_owner_unregister(int task_id, uint32_t task_generation,
                                     void *mutex) {
    if (task_id < 0 || task_generation == 0U || mutex == NULL) return -22;
    uint32_t flags = task_table_lock_irqsave();
    int result = -116;
    if (task_id < num_tasks &&
        tasks[task_id].task_generation == task_generation) {
        task_t *task = &tasks[task_id];
        result = -2;
        for (uint32_t index = 0U; index < task->held_mutex_count; ++index) {
            if (task->held_mutexes[index] != mutex) continue;
            --task->held_mutex_count;
            task->held_mutexes[index] =
                task->held_mutexes[task->held_mutex_count];
            task->held_mutexes[task->held_mutex_count] = NULL;
            result = 0;
            break;
        }
    }
    task_table_unlock_irqrestore(flags);
    return result;
}

static void scheduler_abandon_task_mutexes(int task_id,
                                           uint32_t task_generation) {
    for (uint32_t released = 0U;
         released < SCHEDULER_HELD_MUTEX_CAPACITY; ++released) {
        kernel_mutex_t *mutex = NULL;
        uint32_t flags = task_table_lock_irqsave();
        if (task_id >= 0 && task_id < num_tasks &&
            tasks[task_id].task_generation == task_generation &&
            tasks[task_id].running_cpu == TASK_CPU_NONE &&
            tasks[task_id].held_mutex_count != 0U) {
            task_t *task = &tasks[task_id];
            --task->held_mutex_count;
            mutex = (kernel_mutex_t*)task->held_mutexes[
                task->held_mutex_count];
            task->held_mutexes[task->held_mutex_count] = NULL;
        }
        task_table_unlock_irqrestore(flags);
        if (mutex == NULL) return;
        KASSERT(kernel_mutex_abandon_task_owner(
            mutex, task_id, task_generation));
    }
    uint32_t flags = task_table_lock_irqsave();
    KASSERT(task_id < 0 || task_id >= num_tasks ||
            tasks[task_id].task_generation != task_generation ||
            tasks[task_id].held_mutex_count == 0U);
    task_table_unlock_irqrestore(flags);
}

int scheduler_current_task_id(void) {
    int task_id = -1;
    uint32_t generation = 0U;
    (void)scheduler_current_task_identity(&task_id, &generation);
    return task_id;
}

int scheduler_task_state_snapshot(int task_id, const Process *owner,
                                  uint32_t generation, int *state_out) {
    if (task_id < 0 || owner == NULL || generation == 0U ||
        state_out == NULL) return -22;
    uint32_t flags = task_table_lock_irqsave();
    int result = -3;
    if (task_id < num_tasks && tasks[task_id].process == owner &&
        tasks[task_id].process_generation == generation) {
        *state_out = tasks[task_id].status;
        result = 0;
    }
    task_table_unlock_irqrestore(flags);
    return result;
}

bool scheduler_uses_pit_fallback(void) {
    uint32_t flags = irq_save();
    bool fallback = !apic_timer_active;
    irq_restore(flags);
    return fallback;
}

void scheduler_pit_interrupt_handler(void) {
    if (!scheduler_uses_pit_fallback()) return;
    if (++pit_scheduler_ticks < SCHEDULER_QUANTUM_MS) return;
    pit_scheduler_ticks = 0;
    scheduler_interrupt_handler();
}

void scheduler_interrupt_handler(void) {
    uint64_t timing_start = runtime_timing_begin();
    uint32_t flags = irq_save();
    if (preempt_disable_count != 0) {
        preemption_pending = true;
        runtime_timing_finish_scheduler(timing_start);
        irq_restore(flags);
        return;
    }
    int previous = current_task;
    if (previous >= 0 && previous < num_tasks) {
        validate_running_task_stack_irq_or_panic(&tasks[previous]);
    } else {
        validate_kernel_context_stack_irq_or_panic();
    }
    /* Feeding eligibility is earned only after the scheduler reached its
     * validation point with preemption enabled. Merely receiving IRQ0 does
     * not constitute system progress. */
    if (scheduler_cpu_local()->cpu_index == 0U) watchdog_health_progress();
    uint64_t now_ms = pit_monotonic_ms();
    /* Periodic timers on virtual CPUs commonly fire in the same host time
     * slice.  Waiting here would keep IRQs disabled while the lock owner must
     * itself be scheduled by the hypervisor.  A failed try-lock therefore
     * defers only this periodic decision; the running task remains claimed
     * and the next fixed timer quantum retries it. */
    if (!spinlock_trylock(&task_table_lock)) {
        /* Close the release/publication race with one finite retry. This is
         * not a spin wait: a second collision returns immediately and the
         * next fixed periodic quantum retries the scheduling decision. */
        if (!spinlock_trylock(&task_table_lock)) {
            preemption_pending = true;
            runtime_timing_finish_scheduler(timing_start);
            irq_restore(flags);
            return;
        }
    }
    preemption_pending = false;
    int next = claim_next_runnable(previous, now_ms);
    uint32_t policy_cpu = scheduler_cpu_policy_index_locked();

    bool previous_allowed = previous >= 0 && previous < num_tasks &&
        scheduler_policy_class_allowed(&cpu_windows[policy_cpu],
            tasks[previous].effective_scheduling_class);
    if (next < 0 && previous >= 0 && !previous_allowed &&
        kernel_context_saved) {
        prepare_task_handoff(previous, SCHEDULER_HANDOFF_READY);
        current_task = -1;
        spinlock_release(&task_table_lock);
        activate_task_address_space(-1);
        runtime_timing_finish_scheduler(timing_start);
        swtch(&tasks[previous].context, &kernel_context);
        finish_task_handoff();
        irq_restore(flags);
        return;
    }
    if (next < 0 || next == previous) {
        spinlock_release(&task_table_lock);
        runtime_timing_finish_scheduler(timing_start);
        irq_restore(flags);
        return;
    }

    current_task = next;
    if (previous >= 0)
        prepare_task_handoff(previous, SCHEDULER_HANDOFF_READY);
    spinlock_release(&task_table_lock);
    activate_task_address_space(next);
    runtime_timing_finish_scheduler(timing_start);

    if (previous < 0) {
        kernel_context_saved = true;
        swtch(&kernel_context, &tasks[next].context);
        finish_task_handoff();
    } else {
        swtch(&tasks[previous].context, &tasks[next].context);
        finish_task_handoff();
    }

    irq_restore(flags);
}

void scheduler_preempt_disable(void) {
    uint32_t flags = irq_save();
    KASSERT(preempt_disable_count < UINT32_MAX);
    ++preempt_disable_count;
    irq_restore(flags);
}

void scheduler_preempt_enable(void) {
    uint32_t flags = irq_save();
    KASSERT(preempt_disable_count != 0);
    --preempt_disable_count;
    /* Do not context-switch from inside the unlock path.  The periodic timer
     * will observe preempt_disable_count == 0 on its next tick, clear the
     * pending marker and schedule normally from interrupt context. */
    irq_restore(flags);
}

bool scheduler_preempt_is_disabled(void) {
    uint32_t flags = irq_save();
    bool disabled = preempt_disable_count != 0;
    irq_restore(flags);
    return disabled;
}

bool scheduler_can_sleep(void) {
    return irq_enabled() && !irq_in_context() &&
           !scheduler_preempt_is_disabled();
}

bool scheduler_reserve_termination_locked(int task_id, const Process *owner,
                                          uint32_t generation) {
    KASSERT_IRQ_DISABLED();
    KASSERT(process_table_lock_is_owned());
    if (task_id < 0 || owner == NULL || generation == 0U) return false;
    spinlock_acquire(&task_table_lock);
    bool accepted = false;
    if (task_id < num_tasks && task_id != current_task) {
        task_t *task = &tasks[task_id];
        if (task->process == owner && task->process_generation == generation &&
            owner->generation == generation && owner->is_running &&
            !owner->terminating && task->running_cpu == TASK_CPU_NONE &&
            (task->status == TASK_READY || task->status == TASK_WAITING ||
             task->status == TASK_SLEEPING || task->status == TASK_PREPARED)) {
            wait_queue_cancel_locked(task);
            task->status = TASK_TERMINATION_PENDING;
            accepted = true;
        }
    }
    spinlock_release(&task_table_lock);
    return accepted;
}

void scheduler_terminate_task(int task_id) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    KASSERT(scheduler_preempt_is_disabled());
    uint32_t process_flags = process_table_lock_irqsave();
    spinlock_acquire(&task_table_lock);
    if (task_id < 0 || task_id >= num_tasks) {
        spinlock_release(&task_table_lock);
        process_table_unlock_irqrestore(process_flags);
        return;
    }
    KASSERT(task_id != current_task);

    task_t *task = &tasks[task_id];
    Process *process = task->process;
    uint32_t generation = task->process_generation;
    uint32_t task_generation = task->task_generation;
    if (task->status != TASK_TERMINATION_PENDING ||
        task->running_cpu != TASK_CPU_NONE ||
        process == NULL || process->generation != generation ||
        !process->terminating) {
        spinlock_release(&task_table_lock);
        process_table_unlock_irqrestore(process_flags);
        return;
    }
    task->status = TASK_TERMINATING;
    spinlock_release(&task_table_lock);
    process_table_unlock_irqrestore(process_flags);

    /* VFS teardown may sleep. The reserved state excludes dispatch, wakeup,
     * a second cleanup and premature reaping throughout those sleeps. */
    scheduler_preempt_enable();
    scheduler_abandon_task_mutexes(task_id, task_generation);
    /* Revoke DMA and mask device IRQs before any capability or address-space
     * teardown. The later process-slot cleanup is deliberately idempotent. */
    device_domain_process_cleanup(process->pid, generation);
    ipc_process_cleanup(process->pid, generation);
    storage_request_cancel_process(process->pid, generation);
    framebuffer_frame_process_cleanup(process->pid, generation);
    process_close_all_files(process);
    process_orphan_children(process->pid);
    scheduler_preempt_disable();

    uint32_t flags = process_table_lock_irqsave();
    spinlock_acquire(&task_table_lock);
    KASSERT(task->process == process &&
            task->process_generation == generation &&
            task->task_generation == task_generation &&
            task->status == TASK_TERMINATING &&
            task->running_cpu == TASK_CPU_NONE);
    wait_queue_cancel_locked(task);
    process->exit_status = 143;
    process->has_exited = true;
    process->is_running = false;
    process->terminating = false;
    (void)wait_queue_wake_all_task_locked(&process->exit_waiters, NULL);
    task->status = TASK_FINISHED;
    spinlock_release(&task_table_lock);
    process_table_unlock_irqrestore(flags);
}

void task_exit(void) {
    task_exit_status(0);
}

void task_exit_status(int status) {
    KASSERT_NOT_IRQ();
    KASSERT(!scheduler_preempt_is_disabled());

    uint32_t snapshot_flags = task_table_lock_irqsave();
    int exiting = current_task;
    Process *process = NULL;
    uint32_t process_generation = 0U;
    if (exiting >= 0 && exiting < num_tasks) {
        validate_running_task_stack_or_panic(&tasks[exiting]);
        process = tasks[exiting].process;
        process_generation = tasks[exiting].process_generation;
    }
    task_table_unlock_irqrestore(snapshot_flags);

    /* User exceptions arrive with IF=0. Resource cleanup may sleep in VFS and
     * block-driver mutexes, so enable IRQs without a preemption guard. The
     * running task and terminating process generation remain pinned until the
     * final Process -> Scheduler commit below. */
    irq_enable();
    if (process != NULL) {
        KASSERT(process_begin_exit(process, process_generation));
        device_domain_process_cleanup(process->pid, process_generation);
        ipc_process_cleanup(process->pid, process_generation);
        storage_request_cancel_process(process->pid, process_generation);
        framebuffer_frame_process_cleanup(process->pid, process_generation);
        process_close_all_files(process);
        process_orphan_children(process->pid);
    }
    uint64_t now_ms = pit_monotonic_ms();
    irq_disable();

    uint32_t process_flags = process_table_lock_irqsave();
    spinlock_acquire(&task_table_lock);
    int finished = current_task;
    if (finished >= 0 && finished < num_tasks) {
        wait_queue_cancel_locked(&tasks[finished]);
        tasks[finished].status = TASK_FINISHED;
        if (tasks[finished].process) {
            tasks[finished].process->exit_status = status;
            tasks[finished].process->has_exited = true;
            tasks[finished].process->is_running = false;
            tasks[finished].process->terminating = false;
            (void)wait_queue_wake_all_task_locked(
                &tasks[finished].process->exit_waiters, NULL);
        }
    }

    KASSERT(finished < 0 ||
            tasks[finished].running_cpu ==
                (int32_t)scheduler_cpu_local()->cpu_index);
    int next = claim_next_runnable(finished, now_ms);
    if (next >= 0) {
        if (finished >= 0)
            prepare_task_handoff(finished, SCHEDULER_HANDOFF_RELEASE);
        current_task = next;
        spinlock_release(&task_table_lock);
        process_table_unlock_irqrestore(process_flags);
        activate_task_address_space(next);
        swtch(NULL, &tasks[next].context);
    }

    current_task = -1;
    if (kernel_context_saved) {
        validate_kernel_context_stack_or_panic(false);
        if (finished >= 0)
            prepare_task_handoff(finished, SCHEDULER_HANDOFF_RELEASE);
        spinlock_release(&task_table_lock);
        process_table_unlock_irqrestore(process_flags);
        activate_task_address_space(-1);
        swtch(NULL, &kernel_context);
    }

    spinlock_release(&task_table_lock);
    process_table_unlock_irqrestore(process_flags);
    if (finished >= 0) {
        prepare_task_handoff(finished, SCHEDULER_HANDOFF_RELEASE);
        finish_task_handoff();
    }
    cpu_halt_forever();
    __builtin_unreachable();
}

void scheduler_kill_current(void) {
    task_exit();
}

Process *scheduler_current_process(void) {
    uint32_t flags = task_table_lock_irqsave();
    int index = current_task;
    if (index < 0 || index >= num_tasks || !tasks[index].user_mode) {
        task_table_unlock_irqrestore(flags);
        return NULL;
    }
    Process *process = tasks[index].process;
    task_table_unlock_irqrestore(flags);
    return process;
}

void list_tasks(void) {
    typedef struct {
        uint32_t eip;
        uint32_t esp;
        int status;
        uint8_t scheduling_class;
        uint8_t effective_scheduling_class;
    } task_list_snapshot_t;
    task_list_snapshot_t snapshots[MAX_TASKS];
    scheduler_window_t window;
    uint32_t flags = task_table_lock_irqsave();
    int count = num_tasks;
    if (count > MAX_TASKS) count = MAX_TASKS;
    for (int index = 0; index < count; ++index) {
        snapshots[index] = (task_list_snapshot_t){
            .eip = tasks[index].context.eip,
            .esp = tasks[index].context.esp,
            .status = tasks[index].status,
            .scheduling_class = tasks[index].scheduling_class,
            .effective_scheduling_class =
                tasks[index].effective_scheduling_class,
        };
    }
    window = cpu_windows[scheduler_cpu_policy_index_locked()];
    task_table_unlock_irqrestore(flags);

    printf("Task list (CPU window %u ms, throttled=0x%x):\n",
           SCHEDULER_WINDOW_MS, window.throttled_mask);
    for (int i = 0; i < count; ++i) {
        const char *status = "Ready";
        if (snapshots[i].status == TASK_RUNNING) status = "Running";
        else if (snapshots[i].status == TASK_SLEEPING) status = "Sleeping";
        else if (snapshots[i].status == TASK_WAITING) status = "Waiting";
        else if (snapshots[i].status == TASK_HANDOFF) status = "Handoff";
        else if (snapshots[i].status == TASK_FINISHED) status = "Finished";

        printf("Task %d: EIP=%p, ESP=%p, Status=%s, Class=%u/%u\n",
               i, (void*)(uintptr_t)snapshots[i].eip,
               (void*)(uintptr_t)snapshots[i].esp, status,
               snapshots[i].scheduling_class,
               snapshots[i].effective_scheduling_class);
    }
    for (uint8_t scheduling_class = 0U;
         scheduling_class < SCHEDULER_CLASS_COUNT; ++scheduling_class) {
        printf("CPU class %u: used=%u/%u ms overloads=%u\n",
               scheduling_class, window.used_ms[scheduling_class],
               scheduler_policy_window_limit(scheduling_class),
               window.overload_count[scheduling_class]);
    }
}

int scheduler_resource_stats(scheduler_resource_stats_t *stats_out) {
    if (stats_out == NULL) return -22;
    uint32_t flags = task_table_lock_irqsave();
    uint32_t active = active_task_count_locked();
    if (active > peak_active_tasks) peak_active_tasks = active;
    *stats_out = (scheduler_resource_stats_t){
        .version = SCHEDULER_RESOURCE_STATS_VERSION,
        .struct_size = sizeof(*stats_out),
        .task_capacity = MAX_TASKS,
        .active_tasks = active,
        .peak_active_tasks = peak_active_tasks,
        .capacity_rejections = task_capacity_rejections,
        .supervised_reserve = SUPERVISED_TASK_RESERVE,
        .reserved = kernel_stack_high_water_peak,
    };
    task_table_unlock_irqrestore(flags);
    return 0;
}
