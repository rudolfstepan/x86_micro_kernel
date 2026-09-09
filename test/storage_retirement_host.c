/* Actual Storage lifecycle with explicit, deterministic external boundaries. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RETIREMENT_PROCESS_TEST
#define MAX_PROGRAMS 2
#define KASSERT_NOT_IRQ() ((void)0)
typedef struct { bool is_running, terminating; int pid, task_id; uint32_t generation; } Process;
static Process process_list[MAX_PROGRAMS];
static int process_lock, preempt, reservations, cleanups, finishes;
static bool cpu_owned;
static void scheduler_preempt_disable(void) { ++preempt; }
static void scheduler_preempt_enable(void) { --preempt; }
static uint32_t process_table_lock_irqsave(void) { ++process_lock; return 0; }
static void process_table_unlock_irqrestore(uint32_t f) { (void)f; --process_lock; }
static int scheduler_task_state_snapshot(int task, Process *p, uint32_t g, int *state) {
    if (process_lock != 1 || preempt != 1 || p->task_id != task || p->generation != g) abort();
    *state = 2; return 0;
}
static int scheduler_current_task_id(void) { return 9; }
static bool scheduler_reserve_termination_locked(int task, Process *p, uint32_t g) {
    if (process_lock != 1 || preempt != 1 || p->task_id != task || p->generation != g) abort();
    ++reservations; return !cpu_owned;
}
static void terminal_input_process_cleanup(int pid, uint32_t g) {
    if (pid != 7 || g != 3 || !process_lock) abort();
    ++cleanups;
}
static void scheduler_terminate_task(int task) {
    if (task != 2 || process_lock || !process_list[0].terminating) abort();
    ++finishes;
}
static void task_exit_status(int status) { (void)status; abort(); }
#include "termination.inc"
#define ASSERT(x) do { if (!(x)) { printf("IDENTITY FAIL %d %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    process_list[0] = (Process){true, false, 7, 2, 3};
    ASSERT(process_terminate_identity(7, 0) == -1);
    ASSERT(process_terminate_identity(7, 2) == -1);
    ASSERT(process_terminate_identity(8, 3) == -1);
    ASSERT(!reservations && !cleanups && !finishes);
    cpu_owned = true;
    ASSERT(process_terminate_identity(7, 3) == -1);
    ASSERT(!process_list[0].terminating && !cleanups && !finishes);
    cpu_owned = false;
    ASSERT(process_terminate_identity(7, 3) == 0);
    ASSERT(process_list[0].terminating && cleanups == 1 && finishes == 1);
    ASSERT(process_terminate_identity(7, 3) == -1);
    ASSERT(cleanups == 1 && finishes == 1 && !process_lock && !preempt);
    process_list[0].terminating = false;
    ASSERT(process_terminate(7) == 0 && finishes == 2);
    puts("STORAGE_TERMINATION_IDENTITY_OK"); return 0;
}
#else
#define MAX_DRIVES 16
#define X86_CPU_LOCAL_MAX 16
#define PROCESS_DOMAIN_STORAGE 2
#define TASK_CPU_MASK_BSP 1U
static bool initialized = true, service_administratively_enabled = true;
static bool service_started = true, service_starting;
static volatile uint32_t service_ap_execution_generation, lifecycle_busy;
static volatile bool lifecycle_failed;
static uint8_t saved[128];
static uint64_t now = 10;
static int spawn_count, terminate_count, unbind_count, fence_count, affinity_count;
static bool alive = true;
static bool deny_terminate = true, termination_done, start_fail, publish_fail;
static bool bind_on_sleep, reap_on_sleep;
static int live_pid = 7, boundary_error;
static uint32_t live_generation = 3;
static uint32_t object_fences = 1;
static bool control_valid(const void *, size_t);
static int control_read(void *out) { memcpy(out, saved, 64); return 0; }
static int control_write(const void *in) {
    if (publish_fail) { publish_fail = false; return -84; }
    if (!control_valid(in, 64)) { ++boundary_error; return -84; }
    memcpy(saved, in, 64); return 0;
}
static bool process_identity_alive(int pid, uint32_t generation) {
    return pid == live_pid && generation == live_generation && alive;
}
static int process_terminate_identity(int pid, uint32_t generation) {
    ++terminate_count;
    if (!process_identity_alive(pid, generation) || deny_terminate) return -1;
    termination_done = true;
    return 0; /* Deliberately not reap yet. */
}
static int process_spawn_supervised_prepared(const char *p, int n, const char **a, int d) {
    (void)p; (void)n; (void)a; (void)d;
    if (alive) ++boundary_error;
    ++spawn_count; ++live_pid; ++live_generation; alive = true;
    termination_done = false;
    return live_pid;
}
static int process_start_prepared_supervised(int, uint32_t);
static int process_get_identity(int pid, uint32_t *gen) { (void)pid; *gen = live_generation; return 0; }
static int process_set_supervised_affinity(int p, uint32_t g, uint32_t mask) {
    if (mask == TASK_CPU_MASK_BSP) ++affinity_count;
    return process_identity_alive(p, g) && !termination_done ? 0 : -3;
}
static void storage_request_unbind_service(int p, uint32_t g);
static int storage_request_bind_service(int p, uint32_t g) { (void)p; (void)g; return 0; }
static bool admin_maintenance_init(void) { return true; }
static uint64_t pit_monotonic_ms(void) { return now; }
static uint32_t x86_cpu_current_index(void) { return 0; }
static int scheduler_sleep_ms(uint32_t ms);
static int scheduler_yield(void) { ++now; return 0; }
static int vfs_file_object_guard_poll(uint64_t ms) { (void)ms; return 0; }
static void storage_service_emit_pending_quarantine(void) {}
static void poll_media_reintegration(uint64_t ms) { (void)ms; }
static int vfs_file_object_guard_fenced(uint32_t *mask) { *mask = object_fences; return 0; }
static void storage_fence_writes(void) { ++fence_count; }
static void filesystem_fence_mutations(void) { ++fence_count; }
#include "storage_lifecycle.inc"
#define CHECK(x) do { if (!(x)) { printf("RETIREMENT FAIL line=%d %s\n", __LINE__, #x); return 1; } } while (0)
static void storage_request_unbind_service(int p, uint32_t g) {
    ++unbind_count;
    if (storage_service_authorized(p, g)) ++boundary_error;
}
static int process_start_prepared_supervised(int pid, uint32_t generation) {
    storage_service_control_t control;
    control_read(&control);
    if (control.pid != pid || control.generation != generation || control.healthy || control.retiring)
        ++boundary_error;
    return start_fail ? -3 : 0;
}
static int scheduler_sleep_ms(uint32_t ms) {
    now += ms;
    if (termination_done && reap_on_sleep) alive = false;
    if (bind_on_sleep && alive && !termination_done) {
        bind_on_sleep = false;
        if (storage_service_bind(live_pid, live_generation) != 0) ++boundary_error;
    }
    return 0;
}
static void reset(bool healthy) {
    initialized = service_administratively_enabled = service_started = true;
    service_starting = false; lifecycle_busy = 0; object_fences = 0;
    lifecycle_failed = false;
    spawn_count = terminate_count = unbind_count = fence_count = affinity_count = 0;
    deny_terminate = alive = true;
    live_pid = 7; live_generation = 3;
    termination_done = start_fail = publish_fail = bind_on_sleep = reap_on_sleep = false;
    storage_service_control_t c = {.pid=7, .generation=3, .healthy=healthy, .launch_count=1,
        .start_deadline_ms=healthy ? 0 : now, .post_ready_cpu_affinity_mask=14,
        .quarantined_resources=2, .read_only_resources=2};
    control_write(&c);
}
int main(void) {
    storage_service_control_t control = {.pid=7, .generation=3, .healthy=1, .launch_count=1};
    CHECK(sizeof(control) == 64);
    CHECK(control_valid(&control, sizeof(control)));
    control_write(&control);
    storage_service_poll(now);
    control_read(&control);
    CHECK(terminate_count > 0);
    CHECK(spawn_count == 0);
    CHECK(control.pid == 7 && control.generation == 3);
    CHECK(!storage_service_authorized(7, 3));
    CHECK(storage_service_bind(7, 3) == -13);
    CHECK(control.retiring == STORAGE_RETIRE_PENDING && affinity_count == 1);
    uint64_t deadline = control.start_deadline_ms;
    storage_service_poll(++now);
    control_read(&control);
    CHECK(control.start_deadline_ms == deadline && spawn_count == 0);
    deny_terminate = false;
    storage_service_poll(++now);
    CHECK(termination_done && alive && spawn_count == 0);
    control_read(&control);
    control.admin_down_resources = 8; /* A cleanup callback may change these. */
    control.quarantined_resources |= 4;
    control_write(&control);
    alive = false; /* Only this modeled process-slot release permits replacement. */
    storage_service_poll(++now);
    control_read(&control);
    CHECK(spawn_count == 1 && control.pid == 8 && control.generation == 4);
    CHECK(control.admin_down_resources == 8 && control.quarantined_resources == 4);
    CHECK(!storage_service_authorized(8, 4));
    CHECK(storage_service_bind(8, 4) == 0);
    CHECK(storage_service_authorized(8, 4) && !storage_service_authorized(7, 3));

    /* CPU never yields: one absolute deadline, retained generation and no
     * retries/spawns after exhaustion, even if it disappears afterwards. */
    reset(false);
    storage_service_poll(now);
    control_read(&control);
    deadline = control.start_deadline_ms;
    now = deadline;
    storage_service_poll(now);
    control_read(&control);
    CHECK(control.retiring == STORAGE_RETIRE_EXHAUSTED);
    CHECK(control.pid == 7 && control.generation == 3 && fence_count == 2);
    int old_attempts = terminate_count;
    for (int i=0; i<20; ++i) storage_service_poll(++now);
    CHECK(terminate_count == old_attempts && spawn_count == 0 && fence_count == 2);
    CHECK(storage_service_bind(7, 3) == -13);
    alive = false;
    storage_service_poll(++now);
    CHECK(spawn_count == 0);
    bind_on_sleep = true;
    CHECK(storage_service_component_up(now+1000));
    CHECK(spawn_count == 1 && storage_service_authorized(live_pid, live_generation));

    reset(true);
    CHECK(!storage_service_component_down(now+10));
    control_read(&control);
    CHECK(control.pid == 7 && control.retiring == STORAGE_RETIRE_EXHAUSTED && !service_administratively_enabled);
    CHECK(!storage_service_component_up(now+10));
    CHECK(spawn_count == 0 && alive);
    deny_terminate = false; reap_on_sleep = true;
    CHECK(storage_service_component_down(now+50));
    control_read(&control);
    CHECK(control.pid == 0 && !alive);

    reset(true);
    lifecycle_busy = 1;
    storage_service_poll(now);
    CHECK(!storage_service_component_down(now+100));
    CHECK(!storage_service_component_up(now+100));
    CHECK(!storage_service_start(now));
    CHECK(terminate_count == 0 && unbind_count == 0 && spawn_count == 0);
    lifecycle_busy = 0;

    /* Failed prepared start remains tracked; failed identity publication
     * never admits runnable execution and disables future automatic starts. */
    reset(false); alive = false;
    memset(&control, 0, sizeof(control)); control_write(&control);
    start_fail = true;
    CHECK(!spawn_service(&control, now));
    control_read(&control);
    CHECK(control.pid == live_pid && control.retiring == STORAGE_RETIRE_PENDING);
    CHECK(!storage_service_authorized(live_pid, live_generation));
    reset(true);
    control_read(&control);
    publish_fail = true;
    CHECK(!retirement_begin(&control, now, UINT64_MAX, false));
    CHECK(!storage_service_authorized(7, 3));
    CHECK(!storage_service_component_up(now+100));
    reset(false); alive = false;
    memset(&control, 0, sizeof(control)); control_write(&control);
    publish_fail = true;
    CHECK(!spawn_service(&control, now));
    CHECK(!service_administratively_enabled && spawn_count == 1);
    storage_service_poll(++now);
    CHECK(spawn_count == 1);
    CHECK(deadline_after(UINT64_MAX-3, 1000) == UINT64_MAX);
    CHECK(boundary_error == 0);
    puts("STORAGE_RETIREMENT_OK");
    return 0;
}
#endif
