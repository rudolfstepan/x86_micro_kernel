#ifdef FILE_OBJECT_GUARD_TERMINATE_TEST
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "kernel/sched/wait_queue.h"
#include "scheduler_states.inc"
#define MAX_PROGRAMS 2
typedef struct { int pid, task_id, exit_status; wait_queue_t exit_waiters;
    uint32_t generation; bool terminating, has_exited, is_running; } Process;
typedef struct { int status, running_cpu; Process* process;
    uint32_t process_generation, task_generation, cpu_affinity_mask;
    wait_queue_node_t wait_node; uint64_t wait_deadline_ms;
    int blocked_owner_task, wait_result; uint32_t blocked_owner_generation; } task_t;
static task_t tasks[MAX_TASKS];
static Process process_list[MAX_PROGRAMS];
static int current_task, num_tasks = 2, task_table_lock, assertions, reclaimed;
static int preempt_depth, cleanup_calls, releases, terminal_calls, injected;
static bool interrupts = true, injecting, test_interleaving;
static wait_queue_t sleep_waiters, io_waiters;
static struct { int cpu_index; } cpu_local;
#define KASSERT(x) do { if (!(x)) ++assertions; } while (0)
#define KASSERT_NOT_IRQ() ((void)0)
#define KASSERT_IRQ_DISABLED() KASSERT(!interrupts)
static bool irq_enabled(void) { return interrupts; }
static bool process_table_lock_is_owned(void) { return !interrupts; }
static bool scheduler_preempt_is_disabled(void) { return preempt_depth > 0; }
#define scheduler_cpu_local() (&cpu_local)
static uint32_t process_table_lock_irqsave(void) { uint32_t old = interrupts; interrupts=false; return old; }
static void process_table_unlock_irqrestore(uint32_t flags) { interrupts = flags != 0; }
static void spinlock_acquire(int* lock) { KASSERT(!*lock); ++*lock; }
static void spinlock_release(int* lock) { KASSERT(*lock == 1); --*lock; }
static void assert_task_table_locked(void) { KASSERT(task_table_lock == 1); }
static task_t* task_from_wait_node(wait_queue_node_t* node) {
    return (task_t*)((char*)node - offsetof(task_t, wait_node));
}
static void release_task_resources(task_t* task) { ++releases; task->status=TASK_REAPING; }
#include "scheduler_claim.inc"
#include "scheduler_cancel.inc"
#include "scheduler_wake.inc"
#include "scheduler_wake_all.inc"
#include "scheduler_timeout.inc"
#include "scheduler_sleepers.inc"
#include "scheduler_reap.inc"
#include "scheduler_reserve.inc"
void scheduler_terminate_task(int task_id);
static void scheduler_preempt_disable(void) { ++preempt_depth; }
static void scheduler_preempt_enable(void) {
    KASSERT(preempt_depth > 0); --preempt_depth;
    if (test_interleaving && !preempt_depth && !injecting) {
        injecting = true; ++injected;
        bool saved = interrupts; interrupts = false;
        scheduler_wake_expired_waiters_locked(UINT64_MAX);
        scheduler_wake_expired_sleepers_locked(UINT64_MAX);
        spinlock_acquire(&task_table_lock);
        (void)wait_queue_wake_all_task_locked(&io_waiters, NULL);
        if (claim_task_for_current_cpu(1)) ++reclaimed;
        spinlock_release(&task_table_lock);
        KASSERT(scheduler_reap_finished_task_locked(1, &process_list[1]) == -1);
        interrupts = saved;
        scheduler_preempt_disable();
        scheduler_terminate_task(1);
        scheduler_preempt_enable();
        injecting = false;
    }
}
static void cleanup_probe(void) {
    ++cleanup_calls;
    KASSERT(!scheduler_preempt_is_disabled() && interrupts);
    KASSERT(tasks[1].status == TASK_TERMINATING && tasks[1].running_cpu == TASK_CPU_NONE);
    KASSERT(process_list[1].terminating && process_list[1].is_running);
    KASSERT(tasks[1].wait_node.queue == NULL && tasks[1].wait_deadline_ms == 0);
}
static void scheduler_abandon_task_mutexes(int id, uint32_t gen) { KASSERT(id==1 && gen==3); cleanup_probe(); }
static void device_domain_process_cleanup(int pid, uint32_t gen) { KASSERT(pid==8 && gen==7); cleanup_probe(); }
static void ipc_process_cleanup(int pid, uint32_t gen) { KASSERT(pid==8 && gen==7); cleanup_probe(); }
static void storage_request_cancel_process(int pid, uint32_t gen) { KASSERT(pid==8 && gen==7); cleanup_probe(); }
static void framebuffer_frame_process_cleanup(int pid, uint32_t gen) { KASSERT(pid==8 && gen==7); cleanup_probe(); }
static void process_close_all_files(Process* p) { KASSERT(p==&process_list[1]); cleanup_probe(); }
static void process_orphan_children(int pid) { KASSERT(pid==8); cleanup_probe(); test_interleaving=false; }
static void terminal_input_process_cleanup(int pid, uint32_t gen) { KASSERT(pid==8 && gen==7); ++terminal_calls; }
static int scheduler_current_task_id(void) { return current_task; }
static int scheduler_task_state_snapshot(int id, const Process* owner, uint32_t gen, int* state) {
    if (id<0 || id>=num_tasks || tasks[id].process!=owner || tasks[id].process_generation!=gen) return -1;
    *state=tasks[id].status; return 0;
}
static void task_exit_status(int status) { (void)status; ++assertions; }
#include "scheduler_terminate.inc"
#include "process_terminate.inc"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "terminate:%d: %s assertions=%d\n", __LINE__, #x, assertions); return 1; } } while (0)
static void reset(int state, int cpu) {
    memset(tasks, 0, sizeof(tasks)); memset(process_list, 0, sizeof(process_list));
    wait_queue_init(&io_waiters); wait_queue_init(&sleep_waiters);
    process_list[1]=(Process){.pid=8, .task_id=1, .generation=7, .is_running=true};
    tasks[1]=(task_t){.status=state, .running_cpu=cpu, .process=&process_list[1],
        .process_generation=7, .task_generation=3, .cpu_affinity_mask=1,
        .blocked_owner_task=-1};
    preempt_depth=cleanup_calls=releases=terminal_calls=reclaimed=assertions=injected=0;
    injecting=test_interleaving=false; interrupts=true; task_table_lock=0;
}
int main(void) {
    const int admitted[]={TASK_READY,TASK_WAITING,TASK_SLEEPING,TASK_PREPARED};
    for (unsigned i=0;i<sizeof(admitted)/sizeof(admitted[0]);++i) {
        reset(admitted[i], TASK_CPU_NONE);
        if (admitted[i]==TASK_WAITING || admitted[i]==TASK_SLEEPING) {
            CHECK(wait_queue_insert_ordered_locked(admitted[i]==TASK_WAITING ? &io_waiters : &sleep_waiters,
                &tasks[1].wait_node, 1));
            tasks[1].wait_deadline_ms=1;
            tasks[1].blocked_owner_task=0; tasks[1].blocked_owner_generation=4;
        }
        test_interleaving=true;
        CHECK(process_terminate(8)==0);
        CHECK(!assertions && !reclaimed && !releases && cleanup_calls==7 && terminal_calls==1 && injected==1);
        CHECK(!preempt_depth && interrupts && task_table_lock==0);
        CHECK(tasks[1].status==TASK_FINISHED && tasks[1].wait_node.queue==NULL);
        CHECK(process_list[1].has_exited && !process_list[1].is_running &&
              !process_list[1].terminating && process_list[1].exit_status==143);
        CHECK(process_terminate(8)==-1 && cleanup_calls==7);
        interrupts=false;
        CHECK(scheduler_reap_finished_task_locked(1, &process_list[1])==0 && releases==1);
        CHECK(!assertions);
    }
    const int refused[]={TASK_RUNNING,TASK_HANDOFF,TASK_FINISHED,TASK_REAPING,
                         TASK_TERMINATION_PENDING,TASK_TERMINATING};
    for (unsigned i=0;i<sizeof(refused)/sizeof(refused[0]);++i) {
        reset(refused[i], TASK_CPU_NONE);
        CHECK(process_terminate(8)==-1);
        CHECK(!assertions && !process_list[1].terminating && !terminal_calls && !cleanup_calls);
        CHECK(tasks[1].status==refused[i] && !preempt_depth);
    }
    for (int cpu=0;cpu<2;++cpu) {
        reset(TASK_READY, cpu);
        CHECK(process_terminate(8)==-1 && !process_list[1].terminating);
        CHECK(tasks[1].running_cpu==cpu && tasks[1].status==TASK_READY && !terminal_calls && !cleanup_calls);
    }
    reset(TASK_READY,TASK_CPU_NONE); tasks[1].process_generation=6;
    CHECK(process_terminate(8)==-1 && !process_list[1].terminating && !cleanup_calls);
    reset(TASK_READY,TASK_CPU_NONE); interrupts=false;
    CHECK(!scheduler_reserve_termination_locked(1,&process_list[1],6));
    CHECK(!scheduler_reserve_termination_locked(-1,&process_list[1],7));
    CHECK(!scheduler_reserve_termination_locked(1,NULL,7));
    CHECK(!assertions && tasks[1].status==TASK_READY);
    puts("FILE_OBJECT_GUARD_TERMINATE_OK dispatch-wake-timeout-reap-repeat-generation-cpu-owner");
    return 0;
}
#elif defined(FILE_OBJECT_GUARD_FLOPPY_TEST)
#include <stdio.h>
#include "fs/fat12/fat12_journal.h"
#include "fs/fat12/fat12_remap.h"
#include "fs/fat12/fat12_replica.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "floppy:%d: %s\n", __LINE__, #x); return 1; } } while (0)
static bool fixture_read(void* context, uint32_t sector, void* data) {
    return sector < 2880 && !fseek((FILE*)context, (long)sector * 512, SEEK_SET) &&
        fread(data, 1, 512, (FILE*)context) == 512;
}
static bool fixture_no_write(void* context, uint32_t sector, const void* data) {
    (void)context; (void)sector; (void)data; return false;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    FILE* disk = fopen(argv[1], "rb"); CHECK(disk != NULL);
    uint8_t boot[512]; CHECK(fixture_read(disk, 0, boot));
    uint32_t reserved = boot[14] | ((uint32_t)boot[15] << 8);
    uint32_t fingerprint = boot[39] | ((uint32_t)boot[40] << 8) |
        ((uint32_t)boot[41] << 16) | ((uint32_t)boot[42] << 24);
    uint32_t remap_base = reserved - FAT12_REPLICA_RESERVED_SECTORS - FAT12_REMAP_SPARE_COUNT - 3;
    uint32_t journal_base = remap_base - 2 - FAT12_JOURNAL_MAX_ENTRIES * 2;
    fat12_journal_t journal;
    CHECK(fat12_journal_format(&journal, journal_base, journal_base+1, journal_base+2, fingerprint));
    CHECK(fat12_journal_load(&journal, fixture_read, disk));
    CHECK(fat12_journal_recover(&journal, fixture_read, fixture_no_write, disk));
    fat12_remap_table_t remap;
    CHECK(fat12_remap_format(&remap, remap_base, remap_base+1, remap_base+2, fingerprint));
    CHECK(fat12_remap_load(&remap, fixture_read, disk));
    CHECK(fclose(disk) == 0);
    puts("FILE_OBJECT_GUARD_FLOPPY_OK real-journal-remap-decoders no-effects");
    return 0;
}
#elif defined(FILE_OBJECT_GUARD_EXT2_TEST)
#include <stdio.h>
#include "include/kernel/file_object_guard.h"
#define main original_ext2_lifetime_fixture_main
#include "test/test_reist_vfs_symlink_host.c"
#undef main
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); return __LINE__; } } while (0)
static file_object_guard_t ext2_guard;
static context_t ext2_disk;
static uint8_t ext2_saved[sizeof(ext2_disk.image)];
static unsigned ext2_begins, ext2_ends, ext2_outcome;
static bool ext2_epoch_race, ext2_drop_end;
static const file_object_owner_t ext2_service = {5,3}, ext2_client = {8,7};
static int ext2_test_guard(void* ctx, reist_file_object_guard_request_t* request) {
    (void)ctx;
    if (!file_object_guard_request_valid(request)) return -22;
    if (request->operation == REIST_FILE_OBJECT_SNAPSHOT)
        return file_object_guard_snapshot(&ext2_guard, &request->epoch, ext2_disk.now_ms);
    if (request->operation == REIST_FILE_OBJECT_MUTATION_BEGIN) {
        ++ext2_begins;
        return file_object_guard_begin(&ext2_guard, request->keys,
            request->keys[1].kind ? 2 : 1, request->flags == REIST_FILE_OBJECT_EXCLUSIVE,
            ext2_service, request->epoch, ext2_disk.now_ms,
            request->deadline_ms, &request->token);
    }
    if (request->operation == REIST_FILE_OBJECT_MUTATION_END) {
        ++ext2_ends; ext2_outcome = request->flags;
        if (ext2_drop_end) return -5;
        return file_object_guard_end(&ext2_guard, request->token, ext2_service,
            request->flags, ext2_disk.now_ms);
    }
    return -22;
}
static int ext2_race_read(void* ctx, uint32_t resource, uint32_t sector, uint8_t* data) {
    if (ext2_epoch_race) {
        ext2_epoch_race = false;
        if (file_object_guard_legacy_enter(&ext2_guard, ext2_disk.now_ms)) return -5;
    }
    return read_sector(ctx, resource, sector, data);
}
static void ext2_reset(void) {
    initialize(&ext2_disk); memset(&ext2_guard, 0, sizeof(ext2_guard));
    (void)file_object_guard_init(&ext2_guard);
    ext2_begins = ext2_ends = ext2_outcome = 0;
    ext2_epoch_race = ext2_drop_end = false;
}
static reist_vfs_shadow_ext2_guarded_io_t ext2_test_io(void) {
    reist_vfs_shadow_ext2_guarded_io_t io = {.io=io_for(&ext2_disk), .guard=ext2_test_guard};
    io.io.read_sector = ext2_race_read;
    return io;
}
static int ext2_stop_clean(void* ctx, uint32_t resource, uint32_t sector, const uint8_t* data) {
    if (sector == 50 && read32(data+8) == 0) return -5;
    return write_sector(ctx, resource, sector, data);
}
static int ext2_hold_target(uint32_t* token) {
    uint64_t epoch = 0;
    int status = file_object_guard_snapshot(&ext2_guard, &epoch, ext2_disk.now_ms);
    reist_file_object_key_t key = {.kind=REIST_FILE_OBJECT_EXT2, .resource=1, .object_a=12};
    return status ? status : file_object_guard_pin(&ext2_guard, &key, ext2_service,
        ext2_client, epoch, ext2_disk.now_ms, token);
}
int main(void) {
    ext2_reset();
    reist_vfs_shadow_ext2_guarded_io_t io = ext2_test_io();
    const char* target = "/mnt/ext2/target.txt";
    const char* link = "/mnt/ext2/new-link";
    uint32_t token = 0, mask = 0;
    CHECK(ext2_hold_target(&token) == 0);
    memcpy(ext2_saved, ext2_disk.image, sizeof(ext2_saved));
    CHECK(reist_vfs_shadow_ext2_unlink_guarded(&io, target, (uint32_t)strlen(target),
        ext2_disk.now_ms+4000) == -16);
    CHECK(ext2_begins == 1 && ext2_ends == 0 && !ext2_disk.writes && !ext2_disk.flushes);
    CHECK(!memcmp(ext2_saved, ext2_disk.image, sizeof(ext2_saved)));
    CHECK(reist_vfs_shadow_ext2_rename_guarded(&io, "/mnt/ext2/fast-link", 19,
        link, (uint32_t)strlen(link), ext2_disk.now_ms+4000) == 0);
    CHECK(ext2_outcome == REIST_FILE_OBJECT_DURABLE_COMMIT && ext2_ends == 1);
    CHECK(file_object_guard_verify(&ext2_guard, token, ext2_service, ext2_client, ext2_disk.now_ms) == 0);
    CHECK(file_object_guard_fenced(&ext2_guard, &mask) == 0 && !mask);
    CHECK(file_object_guard_release(&ext2_guard, token, ext2_service, ext2_client) == 0);
    ext2_disk.writes = ext2_disk.flushes = 0;
    ext2_epoch_race = true;
    CHECK(reist_vfs_shadow_ext2_unlink_guarded(&io, target, (uint32_t)strlen(target),
        ext2_disk.now_ms+4000) == -11);
    CHECK(!ext2_disk.writes && !ext2_disk.flushes && ext2_ends == 1);

    /* Capture a genuine durable journal; only guarded recovery may write it. */
    ext2_reset(); io = ext2_test_io();
    reist_vfs_shadow_ext2_io_t capture = io.io; capture.write_sector = ext2_stop_clean;
    CHECK(reist_vfs_shadow_ext2_symlink(&capture, "target.txt", 10, link,
        (uint32_t)strlen(link), ext2_disk.now_ms+4000) == -5);
    CHECK(read32(ext2_disk.image+50*512+8) == 2);
    ext2_disk.writes = ext2_disk.flushes = 0;
    CHECK(ext2_hold_target(&token) == 0);
    x86os_file_info_t info;
    memcpy(ext2_saved, ext2_disk.image, sizeof(ext2_saved));
    CHECK(reist_vfs_shadow_ext2_stat_bounded_guarded(&io, target, (uint32_t)strlen(target),
        0, ext2_disk.now_ms+4000, &info) == -16);
    CHECK(!ext2_disk.writes && !ext2_disk.flushes);
    CHECK(!memcmp(ext2_saved, ext2_disk.image, sizeof(ext2_saved)));
    CHECK(file_object_guard_release(&ext2_guard, token, ext2_service, ext2_client) == 0);
    CHECK(reist_vfs_shadow_ext2_unlink_guarded(&io, target, (uint32_t)strlen(target),
        ext2_disk.now_ms+4000) == -11); /* recovery finished, no user mutation */
    CHECK(ext2_outcome == REIST_FILE_OBJECT_DURABLE_COMMIT && ext2_ends == 1);
    CHECK(ext2_disk.writes == 2 && ext2_disk.flushes == 1);
    CHECK(file_object_guard_fenced(&ext2_guard, &mask) == 0 && !mask);
    CHECK(reist_vfs_shadow_ext2_stat_bounded_guarded(&io, target, (uint32_t)strlen(target),
        0, ext2_disk.now_ms+4000, &info) == 0);

    ext2_reset(); io = ext2_test_io(); ext2_disk.fail_write = 0;
    CHECK(reist_vfs_shadow_ext2_unlink_guarded(&io, target, (uint32_t)strlen(target),
        ext2_disk.now_ms+4000) == -5);
    CHECK(ext2_outcome == REIST_FILE_OBJECT_UNKNOWN && ext2_ends == 1);
    CHECK(file_object_guard_fenced(&ext2_guard, &mask) == 0 && mask == 2);
    ext2_reset(); io = ext2_test_io(); ext2_drop_end = true;
    CHECK(reist_vfs_shadow_ext2_unlink_guarded(&io, target, (uint32_t)strlen(target),
        ext2_disk.now_ms+4000) == -5);
    CHECK(file_object_guard_cleanup(&ext2_guard, ext2_service) == 0);
    CHECK(file_object_guard_fenced(&ext2_guard, &mask) == 0 && mask == 2);
    puts("FILE_OBJECT_GUARD_EXT2_OK namespace-pin unrelated-handle epoch-race read-recovery exclusive durable-eagain failed-write lost-end-fence");
    return 0;
}
#elif defined(FILE_OBJECT_GUARD_SERVICE_TEST)
#include <stdio.h>
#include <string.h>
#include "include/kernel/file_object_guard.h"
#include "userspace/storage/include/reist/vfs_shadow_ext2.h"
#define main original_fat_service_fixture_main
#include "test/test_vfs_shadow_fat32_host.c"
#undef main
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); return __LINE__; } } while (0)
static file_object_guard_t service_guard;
static test_context_t service_disk;
static uint64_t service_clock = 10;
static int service_release_error;
static unsigned service_guard_calls, service_reads;
static bool service_epoch_race;
static int service_guard_call(reist_file_object_guard_request_t* r) {
    ++service_guard_calls;
    if (!file_object_guard_request_valid(r)) return -22;
    file_object_owner_t service = {5, 3}, client = {r->client_pid, r->client_generation};
    switch (r->operation) {
    case REIST_FILE_OBJECT_SNAPSHOT:
        return file_object_guard_snapshot(&service_guard, &r->epoch, service_clock);
    case REIST_FILE_OBJECT_PIN:
        return file_object_guard_pin(&service_guard, &r->keys[0], service, client,
                                     r->epoch, service_clock, &r->token);
    case REIST_FILE_OBJECT_VERIFY:
        return file_object_guard_verify(&service_guard, r->token, service, client, service_clock);
    case REIST_FILE_OBJECT_RELEASE:
        return service_release_error ? service_release_error :
            file_object_guard_release(&service_guard, r->token, service, client);
    default: return -22;
    }
}
#define x86os_file_object_guard service_guard_call
int x86os_monotonic_ms(uint64_t* now) { *now = service_clock; return 0; }
int x86os_process_identity_of(int pid, x86os_process_identity_t* identity) {
    *identity = (x86os_process_identity_t){.version=1, .struct_size=sizeof(*identity),
        .pid=pid, .generation=7};
    return pid == 8 || pid == 9 ? 0 : -3;
}
static int vfs_shadow_drive_info(void* ctx, uint32_t resource, x86os_drive_info_t* info) {
    (void)ctx; return drive_info(&service_disk, resource, info);
}
static int vfs_shadow_read_sector(void* ctx, uint32_t resource, uint32_t sector, uint8_t* data) {
    (void)ctx; ++service_reads;
    if (service_epoch_race) {
        service_epoch_race = false;
        if (file_object_guard_legacy_enter(&service_guard, service_clock)) return -5;
    }
    return read_sector(&service_disk, resource, sector, data);
}
#define VFS_EXT2_READ_DEADLINE_MS 2000U
static int vfs_shadow_deadline(uint32_t budget, uint64_t* deadline) {
    *deadline = service_clock + budget; return 0;
}
static int service_time(void* ctx, uint64_t* now) { (void)ctx; return x86os_monotonic_ms(now); }
static int service_guard_adapter(void* ctx, reist_file_object_guard_request_t* request) {
    (void)ctx; return service_guard_call(request);
}
static reist_vfs_shadow_ext2_guarded_io_t vfs_shadow_ext2_io(void) {
    return (reist_vfs_shadow_ext2_guarded_io_t){.io={.drive_info=vfs_shadow_drive_info,
        .read_sector=vfs_shadow_read_sector, .monotonic_ms=service_time},
        .guard=service_guard_adapter};
}
static uint8_t vfs_bulk_data[X86OS_STORAGE_BULK_MAX_BYTES];
static uint32_t format_crc32(const uint8_t* data, uint32_t length) {
    (void)data; return length; /* bulk transport CRC is covered by its own suite */
}
#include "service_objects.inc"
static x86os_vfs_shadow_object_frame_t service_open_frame(void) {
    x86os_vfs_shadow_object_frame_t frame = {0};
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION; frame.struct_size = sizeof(frame);
    frame.operation = X86OS_VFS_SHADOW_OBJECT_OPEN_RIGHTS;
    frame.flags = X86OS_VFS_OBJECT_RIGHT_ALL;
    strcpy(frame.path, "/README.TXT"); frame.path_length = (uint32_t)strlen(frame.path);
    return frame;
}
int main(void) {
    initialize_fat32(&service_disk);
    CHECK(file_object_guard_init(&service_guard) == 0);
    x86os_vfs_shadow_object_frame_t frame = service_open_frame();
    CHECK(vfs_object_control(&frame, 8, 7, 3) == 0 && frame.result == 0 && frame.object_token);
    uint32_t count = 0;
    CHECK(file_object_guard_count(&service_guard, 0, &count, service_clock) == 0 && count == 1);
    uint32_t source_token = frame.object_token;
    vfs_object_slot_t *source = &vfs_objects[(source_token & 255U) - 1U];
    uint64_t epoch = 0;
    uint32_t mutation = 0;
    CHECK(file_object_guard_snapshot(&service_guard, &epoch, service_clock) == 0);
    CHECK(file_object_guard_begin(&service_guard, &source->key, 1, false,
        (file_object_owner_t){5,3}, epoch, service_clock, service_clock+100,
        &mutation) == -REIST_EBUSY);
    x86os_vfs_shadow_object_delegate_frame_t delegate = {0};
    delegate.version = X86OS_VFS_SHADOW_FRAME_VERSION; delegate.struct_size = sizeof(delegate);
    delegate.operation = X86OS_VFS_SHADOW_OBJECT_DELEGATE;
    delegate.object_token = source_token; delegate.service_generation = 3;
    delegate.target_pid = 9; delegate.target_generation = 7;
    delegate.rights = X86OS_VFS_OBJECT_RIGHT_STAT;
    CHECK(vfs_object_delegate(&delegate, 8, 7, 3) == 0 && delegate.result == 0);
    CHECK(file_object_guard_count(&service_guard, 0, &count, service_clock) == 0 && count == 2);
    frame = (x86os_vfs_shadow_object_frame_t){.version=X86OS_VFS_SHADOW_FRAME_VERSION,
        .struct_size=sizeof(frame), .operation=X86OS_VFS_SHADOW_OBJECT_ADOPT};
    CHECK(vfs_object_control(&frame, 9, 7, 3) == 0 && frame.result == 0);
    uint32_t adopted = frame.object_token;
    vfs_object_discard_reply(source_token, 8, 7, 3);
    CHECK(file_object_guard_count(&service_guard, 0, &count, service_clock) == 0 && count == 1);
    x86os_vfs_shadow_object_read_frame_t read = {.version=X86OS_VFS_SHADOW_FRAME_VERSION,
        .struct_size=sizeof(read), .operation=X86OS_VFS_SHADOW_OBJECT_READ,
        .object_token=adopted, .service_generation=3, .requested=1};
    unsigned before_reads = service_reads;
    CHECK(vfs_object_read(&read, 9, 7, 3) == 0 && read.result == -13);
    CHECK(service_reads == before_reads);
    frame = (x86os_vfs_shadow_object_frame_t){.version=X86OS_VFS_SHADOW_FRAME_VERSION,
        .struct_size=sizeof(frame), .operation=X86OS_VFS_SHADOW_OBJECT_CLOSE,
        .object_token=adopted, .service_generation=3};
    service_release_error = -16;
    CHECK(vfs_object_control(&frame, 9, 7, 3) == 0 && frame.result == -16);
    CHECK(file_object_guard_count(&service_guard, 0, &count, service_clock) == 0 && count == 1);
    service_release_error = 0;
    for (unsigned i=0; i<VFS_OBJECT_CAPACITY; ++i) vfs_object_reap_one();
    CHECK(file_object_guard_count(&service_guard, 0, &count, service_clock) == 0 && count == 0);
    frame = service_open_frame(); service_epoch_race = true;
    CHECK(vfs_object_control(&frame, 8, 7, 3) == 0 && frame.result == -11 && !frame.object_token);
    CHECK(file_object_guard_count(&service_guard, 0, &count, service_clock) == 0 && count == 0);
    frame = service_open_frame();
    CHECK(vfs_object_control(&frame, 8, 7, 3) == 0 && frame.result == 0);
    CHECK(file_object_guard_revoke_media(&service_guard, 0) == 0);
    read = (x86os_vfs_shadow_object_read_frame_t){.version=X86OS_VFS_SHADOW_FRAME_VERSION,
        .struct_size=sizeof(read), .operation=X86OS_VFS_SHADOW_OBJECT_READ,
        .object_token=frame.object_token, .service_generation=3, .requested=1};
    before_reads = service_reads;
    CHECK(vfs_object_read(&read, 8, 7, 3) == 0 && read.result == -116 && !read.transferred);
    CHECK(service_reads == before_reads);
    puts("FILE_OBJECT_GUARD_SERVICE_OK real-service real-fat real-core pin-admission delegation rights reply-loss close-retry epoch-race media-revoke");
    return 0;
}
#elif defined(FILE_OBJECT_GUARD_VFS_TEST)
#include <stdio.h>
#define main original_vfs_fixture_main
#include "test/test_vfs_host.c"
#undef main
#include "include/kernel/file_object_guard.h"
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); return __LINE__; } } while (0)

static drive_t guard_test_drives[3];
static uint64_t guard_test_time = 10;
static uint32_t guard_test_quarantine;
static bool guard_client_live = true, guard_service_live = true;
static uint32_t guard_client_generation = 7, guard_service_generation = 3;
drive_t* vfs_guard_platform_drive(uint32_t resource) {
    return resource < 3 ? &guard_test_drives[resource] : NULL;
}
uint64_t vfs_guard_platform_now(void) { return guard_test_time; }
bool vfs_guard_platform_live(int pid, uint32_t generation) {
    return (pid == 5 && generation == guard_service_generation && guard_service_live) ||
        (pid == 8 && generation == guard_client_generation && guard_client_live);
}
bool vfs_guard_platform_available(uint32_t resource) {
    return resource < 3 && !(guard_test_quarantine & (1U << resource));
}
bool vfs_guard_platform_fence(uint32_t resource) {
    guard_test_quarantine |= 1U << resource;
    return true;
}

#define MAX_PROCESS_DESCRIPTORS 4
typedef struct { int pid; uint32_t generation;
    struct { bool in_use; } files[MAX_PROCESS_DESCRIPTORS]; } Process;
typedef struct { int unused; } page_directory_t;
static Process syscall_process = {.pid=5, .generation=3};
static int process_file_close(Process* process, int fd) {
    process->files[fd].in_use = false; return 0;
}
#include "process_cleanup.inc"
static page_directory_t syscall_directory;
static unsigned syscall_ranges, syscall_reads, syscall_writes, syscall_terminations;
static bool syscall_readable = true, syscall_writable = true, syscall_copyout_fault;
static Process* scheduler_current_process(void) { return &syscall_process; }
static bool storage_service_authorized(int pid, uint32_t generation) {
    return pid == 5 && generation == guard_service_generation && guard_service_live;
}
static page_directory_t* paging_current_directory(void) { return &syscall_directory; }
static bool user_range_accessible(page_directory_t* directory, uint32_t address,
                                   size_t size, bool writable) {
    ++syscall_ranges;
    return directory == &syscall_directory && address && size == 112 &&
        (writable ? syscall_writable : syscall_readable);
}
static int copy_from_user(void* target, const void* source, size_t size) {
    ++syscall_reads; memcpy(target, source, size); return 0;
}
static int copy_to_user(void* target, const void* source, size_t size) {
    ++syscall_writes;
    if (syscall_copyout_fault) { guard_test_time += 1000; return -1; }
    memcpy(target, source, size); return 0;
}
static int process_terminate(int pid) { (void)pid; ++syscall_terminations; return 0; }
#include "syscall_guard.inc"

static int integration_key(const vfs_node_t* node, reist_file_object_key_t* key) {
    memset(key, 0, sizeof(*key));
    key->kind = REIST_FILE_OBJECT_EXT2;
    key->object_a = node->inode;
    return VFS_OK;
}
static int integration_extent(const vfs_filesystem_t* fs,
                                uint32_t* first, uint32_t* count) {
    *first = 0; /* Like the real FAT/EXT2 backend on a partition device. */
    *count = fs->drive->sectors;
    return VFS_OK;
}
static reist_file_object_guard_request_t integration_request(uint32_t operation) {
    reist_file_object_guard_request_t request = {0};
    request.version = REIST_FILE_OBJECT_VERSION;
    request.struct_size = sizeof(request);
    request.operation = operation;
    return request;
}
static uint64_t integration_epoch(void) {
    reist_file_object_guard_request_t request = integration_request(REIST_FILE_OBJECT_SNAPSHOT);
    if (vfs_file_object_guard_request(&request, 5, 3) != 0) return 0;
    return request.epoch;
}

static int integration_syscall_boundaries(void) {
    syscall_process.pid = 9;
    CHECK(syscall_file_object_guard(NULL) == -REIST_EACCES);
    CHECK(syscall_ranges == 0 && syscall_reads == 0 && syscall_writes == 0);
    syscall_process.pid = 5;
    reist_file_object_guard_request_t request = integration_request(REIST_FILE_OBJECT_SNAPSHOT);
    syscall_writable = false;
    CHECK(syscall_file_object_guard(&request) == -REIST_EFAULT);
    CHECK(syscall_reads == 0 && syscall_writes == 0);
    syscall_writable = true;
    request.reserved = 1;
    CHECK(syscall_file_object_guard(&request) == -REIST_EINVAL && request.epoch == 0);
    request.reserved = 0;
    CHECK(syscall_file_object_guard(&request) == 0 && request.epoch);
    request.operation = REIST_FILE_OBJECT_PIN;
    request.keys[0] = (reist_file_object_key_t){.kind=REIST_FILE_OBJECT_EXT2, .object_a=42};
    request.client_pid = 8; request.client_generation = 7;
    syscall_copyout_fault = true;
    CHECK(syscall_file_object_guard(&request) == -REIST_EFAULT && request.token == 0);
    CHECK(vfs_delete("/file") == VFS_OK); /* no undelivered pin survives */
    request = integration_request(REIST_FILE_OBJECT_MUTATION_BEGIN);
    request.keys[0] = (reist_file_object_key_t){.kind=REIST_FILE_OBJECT_EXT2, .object_a=42};
    request.epoch = integration_epoch(); request.deadline_ms = guard_test_time + 50;
    /* Prove a failed copyout that crosses the deadline is still known-no-effect. */
    CHECK(syscall_file_object_guard(&request) == -REIST_EFAULT && request.token == 0);
    syscall_copyout_fault = false;
    CHECK(integration_epoch() != 0 && vfs_delete("/file") == VFS_OK);
    CHECK(syscall_terminations == 0);
    puts("FILE_OBJECT_GUARD_SYSCALL_OK authority null-fields range-before-effects copyout-rollback");
    return 0;
}

static int integration_lifecycle(void) {
    reist_file_object_guard_request_t pin = integration_request(REIST_FILE_OBJECT_PIN);
    pin.keys[0] = (reist_file_object_key_t){.kind=REIST_FILE_OBJECT_EXT2, .object_a=42};
    pin.client_pid = 8; pin.client_generation = 7; pin.epoch = integration_epoch();
    CHECK(vfs_file_object_guard_request(&pin, 5, 3) == 0);
    vfs_file_object_guard_process_cleanup(8, 8); /* stale generation cannot release */
    CHECK(vfs_delete("/file") == VFS_ERR_BUSY);
    Process exiting = {.pid=8, .generation=7};
    process_close_all_files(&exiting); /* normal/fault/terminate teardown hook */
    process_close_all_files(&exiting); /* exact-generation idempotence */
    CHECK(vfs_delete("/file") == VFS_OK);
    pin.token = 0; pin.epoch = integration_epoch();
    CHECK(vfs_file_object_guard_request(&pin, 5, 3) == 0);
    guard_client_live = false;
    for (unsigned i = 0; i < 16; ++i) CHECK(vfs_file_object_guard_poll(guard_test_time) == 0);
    CHECK(vfs_delete("/file") == VFS_OK);
    guard_client_generation = 8; guard_client_live = true;
    reist_file_object_guard_request_t verify = integration_request(REIST_FILE_OBJECT_VERIFY);
    verify.token = pin.token; verify.client_pid = 8; verify.client_generation = 7;
    CHECK(vfs_file_object_guard_request(&verify, 5, 3) == -REIST_ESTALE);
    pin.token = 0; pin.client_generation = 8; pin.epoch = integration_epoch();
    CHECK(vfs_file_object_guard_request(&pin, 5, 3) == 0);
    reist_file_object_guard_request_t other = pin;
    other.token = 0; other.keys[0].resource = 1; other.epoch = integration_epoch();
    CHECK(vfs_file_object_guard_request(&other, 5, 3) == 0);
    vfs_node_t* old_legacy = NULL;
    CHECK(vfs_open("/file", &old_legacy) == VFS_OK);
    vfs_file_object_guard_media_changed(2); /* physical alias must revoke resource0 */
    verify.client_generation = 8; verify.token = pin.token;
    CHECK(vfs_file_object_guard_request(&verify, 5, 3) == -REIST_ESTALE);
    verify.token = other.token;
    CHECK(vfs_file_object_guard_request(&verify, 5, 3) == 0);
    verify.operation = REIST_FILE_OBJECT_RELEASE;
    CHECK(vfs_file_object_guard_request(&verify, 5, 3) == 0);
    uint8_t byte = 0;
    vfs_dir_entry_t info;
    CHECK(vfs_read(old_legacy, 0, 1, &byte) == VFS_ERR_IO);
    CHECK(vfs_fstat(old_legacy, &info) == VFS_ERR_IO);
    CHECK(vfs_write(old_legacy, 0, 1, &byte) == VFS_ERR_IO);
    CHECK(vfs_stat("/file", &info) == VFS_ERR_IO);
    CHECK(vfs_close(old_legacy) == VFS_OK);
    CHECK(vfs_open("/file", &old_legacy) == VFS_ERR_IO);
    CHECK(vfs_unmount("/") == VFS_OK);
    CHECK(vfs_mount(&guard_test_drives[0], "ext2", "/") == VFS_OK);
    reist_file_object_guard_request_t begin = integration_request(REIST_FILE_OBJECT_MUTATION_BEGIN);
    begin.keys[0] = pin.keys[0]; begin.epoch = integration_epoch();
    begin.deadline_ms = guard_test_time + 100;
    CHECK(vfs_file_object_guard_request(&begin, 5, 3) == 0);
    guard_service_live = false;
    CHECK(vfs_file_object_guard_poll(guard_test_time) == 0);
    CHECK(guard_test_quarantine == 1);
    uint32_t mask = 0;
    CHECK(vfs_file_object_guard_fenced(&mask) == 0 && mask == 1);
    guard_service_generation = 4; guard_service_live = true;
    pin.token = 0;
    reist_file_object_guard_request_t snapshot = integration_request(REIST_FILE_OBJECT_SNAPSHOT);
    CHECK(vfs_file_object_guard_request(&snapshot, 5, 4) == 0);
    pin.epoch = snapshot.epoch;
    CHECK(vfs_file_object_guard_request(&pin, 5, 4) == -REIST_EIO);
    vfs_node_t* unrelated = NULL;
    CHECK(vfs_open("/other/file", &unrelated) == VFS_OK && vfs_close(unrelated) == VFS_OK);
    puts("FILE_OBJECT_GUARD_LIFECYCLE_OK owner-sweep media-alias-revoke service-loss-fence fresh-generation-denied unrelated-read");
    return 0;
}

int main(void) {
    /* Old host fixture remains a separate executable: this one enables the
     * production guard branch, linking the real VFS and real metadata core. */
    vfs_init();
    guard_test_drives[0].type = DRIVE_TYPE_ATA;
    guard_test_drives[0].base = 0x1f0;
    guard_test_drives[0].is_master = true;
    guard_test_drives[0].sectors = 4096;
    strcpy(guard_test_drives[0].name, "disk0");
    guard_test_drives[1] = guard_test_drives[0];
    guard_test_drives[1].base = 0x170;
    strcpy(guard_test_drives[1].name, "disk1");
    guard_test_drives[2] = guard_test_drives[0]; /* exact physical alias */
    strcpy(guard_test_drives[2].name, "alias");
    fake_ops.object_key = integration_key;
    fake_ops.volume_extent = integration_extent;
    CHECK(vfs_register_filesystem("ext2", &fake_ops) == VFS_OK);
    guard_test_drives[2].type = DRIVE_TYPE_PARTITION;
    guard_test_drives[2].parent_resource = 0;
    guard_test_drives[2].lba_offset = 1024;
    guard_test_drives[2].sectors = 1000;
    CHECK(vfs_mount(&guard_test_drives[2], "ext2", "/part") == VFS_OK);
    vfs_node_t* partition_file = NULL;
    CHECK(vfs_open("/part/file", &partition_file) == VFS_OK);
    CHECK(vfs_close(partition_file) == VFS_OK);
    reist_file_object_guard_request_t partition_mutation = integration_request(REIST_FILE_OBJECT_MUTATION_BEGIN);
    partition_mutation.keys[0] = (reist_file_object_key_t){.kind=REIST_FILE_OBJECT_EXT2,
        .resource=2, .object_a=42};
    partition_mutation.epoch = integration_epoch(); partition_mutation.deadline_ms = 100;
    CHECK(vfs_file_object_guard_request(&partition_mutation, 5, 3) == 0);
    CHECK(vfs_file_object_guard_io_begin(2, 20, false, 5, 3) == 0);
    vfs_file_object_guard_io_end();
    CHECK(vfs_file_object_guard_io_begin(2, 1000, false, 5, 3) == -REIST_EINVAL);
    CHECK(vfs_file_object_guard_io_begin(0, 1044, false, 5, 3) == -REIST_EINVAL);
    reist_file_object_guard_request_t partition_end = integration_request(REIST_FILE_OBJECT_MUTATION_END);
    partition_end.token = partition_mutation.token; partition_end.flags = REIST_FILE_OBJECT_NO_EFFECT;
    CHECK(vfs_file_object_guard_request(&partition_end, 5, 3) == 0);
    CHECK(vfs_unmount("/part") == VFS_OK);
    guard_test_drives[2] = guard_test_drives[0];
    CHECK(vfs_mount(&guard_test_drives[0], "ext2", "/") == VFS_OK);
    CHECK(vfs_mount(&guard_test_drives[1], "ext2", "/other") == VFS_OK);
    reist_file_object_guard_request_t opened = integration_request(REIST_FILE_OBJECT_PIN);
    opened.keys[0] = (reist_file_object_key_t){.kind=REIST_FILE_OBJECT_EXT2,
        .resource=2, .object_a=42};
    opened.client_pid = 8; opened.client_generation = 7;
    opened.epoch = integration_epoch();
    CHECK(opened.epoch != 0);
    CHECK(vfs_file_object_guard_request(&opened, 5, 3) == 0 && opened.token);
    CHECK(vfs_file_object_guard_io_begin(0, 20, false, 5, 3) == -REIST_EACCES);
    CHECK(vfs_file_object_guard_io_begin(2, 20, false, 5, 3) == -REIST_EACCES);
    CHECK(vfs_delete("/alias") == VFS_ERR_BUSY && fake_delete_calls == 0);
    CHECK(vfs_rename("/other-file", "/alias") == VFS_ERR_BUSY && fake_rename_calls == 0);
    CHECK(vfs_delete("/other-file") == VFS_OK && fake_delete_calls == 1);
    CHECK(vfs_maintenance_acquire(&guard_test_drives[0]) == VFS_ERR_BUSY);
    CHECK(vfs_unmount("/") == VFS_ERR_BUSY);
    reist_file_object_guard_request_t verify = integration_request(REIST_FILE_OBJECT_VERIFY);
    verify.token = opened.token; verify.client_pid = 8; verify.client_generation = 7;
    CHECK(vfs_file_object_guard_request(&verify, 5, 3) == 0);
    verify.operation = REIST_FILE_OBJECT_RELEASE;
    CHECK(vfs_file_object_guard_request(&verify, 5, 3) == 0);
    CHECK(vfs_delete("/alias") == VFS_OK && fake_delete_calls == 2);
    opened.token = 0; opened.epoch = integration_epoch();
    CHECK(vfs_delete("/other-file") == VFS_OK);
    CHECK(vfs_file_object_guard_request(&opened, 5, 3) == -REIST_EAGAIN);
    vfs_node_t* legacy = NULL;
    CHECK(vfs_open("/file", &legacy) == VFS_OK);
    reist_file_object_guard_request_t mutation = integration_request(REIST_FILE_OBJECT_MUTATION_BEGIN);
    mutation.keys[0] = opened.keys[0];
    mutation.epoch = integration_epoch(); mutation.deadline_ms = 100;
    CHECK(vfs_file_object_guard_request(&mutation, 5, 3) == -REIST_EBUSY);
    fake_close_fails = true;
    CHECK(vfs_close(legacy) == VFS_ERR_IO);
    CHECK(vfs_file_object_guard_request(&mutation, 5, 3) == -REIST_EBUSY);
    fake_close_fails = false;
    CHECK(vfs_close(legacy) == VFS_OK);
    CHECK(vfs_file_object_guard_request(&mutation, 5, 3) == 0);
    CHECK(vfs_file_object_guard_io_begin(0, 20, false, 5, 3) == 0);
    vfs_file_object_guard_io_end();
    CHECK(vfs_file_object_guard_io_begin(2, 0, true, 5, 3) == 0);
    vfs_file_object_guard_io_end();
    CHECK(vfs_file_object_guard_io_begin(1, 20, false, 5, 3) == -REIST_EBUSY);
    CHECK(vfs_file_object_guard_io_begin(0, 4096, false, 5, 3) == -REIST_EINVAL);
    CHECK(vfs_file_object_guard_io_begin(0, 20, false, 5, 4) == -REIST_EACCES);
    CHECK(vfs_open("/file", &legacy) == VFS_ERR_BUSY && legacy == NULL);
    CHECK(vfs_open("/other/file", &legacy) == VFS_OK);
    CHECK(vfs_close(legacy) == VFS_OK);
    CHECK(vfs_delete("/file") == VFS_ERR_BUSY);
    reist_file_object_guard_request_t end = integration_request(REIST_FILE_OBJECT_MUTATION_END);
    end.token = mutation.token; end.flags = REIST_FILE_OBJECT_NO_EFFECT;
    CHECK(vfs_file_object_guard_request(&end, 5, 3) == 0);
    CHECK(vfs_open("/file", &legacy) == VFS_OK && vfs_close(legacy) == VFS_OK);
    CHECK(integration_syscall_boundaries() == 0);
    CHECK(integration_lifecycle() == 0);
    puts("FILE_OBJECT_GUARD_VFS_OK both-registries aliases epoch close-failure maintenance");
    return 0;
}
#else
/* Include the actual implementation for controlled metadata-corruption and
 * lock-contention injection. No alternate host implementation of the guard. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include "kernel/init/file_object_guard.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #x); exit(1); } } while (0)

#define main original_fat_fixture_main
#include "test/test_vfs_shadow_fat32_host.c"
#undef main

static file_object_guard_t fixture;
static const file_object_owner_t service = {5, 3};
static const file_object_owner_t client = {8, 7};

static reist_file_object_key_t key(uint32_t object) {
    reist_file_object_key_t result = {0};
    result.kind = REIST_FILE_OBJECT_EXT2;
    result.resource = 1;
    result.object_a = object;
    return result;
}

static void reset_fixture(void) {
    memset(&fixture, 0, sizeof(fixture));
    CHECK(file_object_guard_init(&fixture) == 0);
}

static uint64_t snapshot(void) {
    uint64_t epoch = 0;
    CHECK(file_object_guard_snapshot(&fixture, &epoch, 10) == 0);
    CHECK(epoch != 0);
    return epoch;
}

static uint32_t fence_mask(void) {
    uint32_t mask = 0;
    int result = file_object_guard_fenced(&fixture, &mask);
    CHECK(result == 0 || (result == -REIST_EIO && mask == UINT32_MAX));
    return mask;
}

static uint32_t pin(reist_file_object_key_t object, file_object_owner_t owner) {
    uint32_t token = 0;
    CHECK(file_object_guard_pin(&fixture, &object, service, owner,
        snapshot(), 10, &token) == 0);
    CHECK(token != 0);
    return token;
}

static void admission_and_namespace(void) {
    reset_fixture();
    reist_file_object_key_t a = key(12), b = key(13), keys[2] = {a, b};
    uint32_t token = pin(a, client), mutation = 0;
    uint64_t epoch = snapshot();
    CHECK(file_object_guard_begin(&fixture, keys, 2, false, service,
        epoch, 10, 100, &mutation) == -REIST_EBUSY);
    CHECK(mutation == 0);
    CHECK(file_object_guard_begin(&fixture, &b, 1, false, service,
        epoch, 10, 100, &mutation) == 0);
    uint32_t undelivered = 0;
    CHECK(file_object_guard_pin(&fixture, &a, service, client,
        epoch, 10, &undelivered) == -REIST_EBUSY);
    CHECK(undelivered == 0);
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == 0);
    CHECK(file_object_guard_end(&fixture, mutation, service,
        REIST_FILE_OBJECT_DURABLE_COMMIT, 11) == 0);
    CHECK(file_object_guard_pin(&fixture, &a, service, client,
        epoch, 12, &undelivered) == -REIST_EAGAIN);
    CHECK(file_object_guard_verify(&fixture, token, service, client, 12) == 0);
    CHECK(file_object_guard_release(&fixture, token, service, client) == 0);
    CHECK(file_object_guard_release(&fixture, token, service, client) == 0);
    CHECK(file_object_guard_begin(&fixture, keys, 2, false, service,
        snapshot(), 12, 100, &mutation) == 0);
    CHECK(file_object_guard_end(&fixture, mutation, service,
        REIST_FILE_OBJECT_NO_EFFECT, 13) == 0);
    uint32_t replacement = pin(a, client);
    CHECK(replacement != token);
    CHECK(file_object_guard_verify(&fixture, token, service, client, 14) == -REIST_ESTALE);
    CHECK(file_object_guard_release(&fixture, token, service, client) == -REIST_ESTALE);
    CHECK(file_object_guard_verify(&fixture, replacement, service, client, 14) == 0);
}

static void lifecycle_and_recovery(void) {
    reset_fixture();
    reist_file_object_key_t a = key(12), b = key(13);
    uint32_t token = pin(a, client), mutation = 0;
    CHECK(file_object_guard_begin(&fixture, &b, 1, true, service,
        snapshot(), 10, 100, &mutation) == -REIST_EBUSY);
    file_object_owner_t stale = {client.pid, client.generation + 1};
    CHECK(file_object_guard_cleanup(&fixture, stale) == 0);
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == 0);
    CHECK(file_object_guard_cleanup(&fixture, client) == 0);
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == -REIST_ESTALE);
    CHECK(file_object_guard_begin(&fixture, &b, 1, true, service,
        snapshot(), 10, 100, &mutation) == 0);
    CHECK(file_object_guard_cleanup(&fixture, service) == 0);
    CHECK(fence_mask() == (1U << 1));
    CHECK(file_object_guard_end(&fixture, mutation, service,
        REIST_FILE_OBJECT_DURABLE_COMMIT, 11) == -REIST_ESTALE);
    CHECK(file_object_guard_pin(&fixture, &a, service, client,
        snapshot(), 10, &token) == -REIST_EIO);
    a.resource = 2;
    pin(a, client); /* unrelated volume is still usable */
    CHECK(file_object_guard_init(&fixture) == 0); /* restart cannot clear fence */
    CHECK(fence_mask() == (1U << 1));
}

static void limits_expiry_and_integrity(void) {
    reset_fixture();
    reist_file_object_key_t a = key(12);
    uint32_t token = 0;
    uint64_t epoch = snapshot();
    CHECK(file_object_guard_begin(&fixture, &a, 1, false, service,
        epoch, 10, 5011, &token) == -REIST_EINVAL);
    CHECK(file_object_guard_begin(&fixture, &a, 1, false, service,
        epoch, 10, 100, &token) == 0);
    CHECK(file_object_guard_poll(&fixture, 100) == 0);
    CHECK(fence_mask() == (1U << 1));
    reset_fixture();
    token = pin(a, client);
    fixture.lock = 1;
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == -REIST_EBUSY);
    uint32_t mask = 42;
    CHECK(file_object_guard_fenced(&fixture, &mask) == -REIST_EBUSY);
    CHECK(mask == 42 && fixture.poisoned == 0);
    fixture.lock = 0;
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == 0);
    /* Two-bit damage in both replicas: no silent owner disposal. */
    fixture.pins[0].primary.words[4] ^= 3;
    fixture.pins[0].shadow.words[4] ^= 3;
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == -REIST_EIO);
    CHECK(fence_mask() == UINT32_MAX);
    CHECK(file_object_guard_init(&fixture) == -REIST_EIO);
}

static void quotas_and_media(void) {
    reset_fixture();
    uint32_t tokens[FILE_OBJECT_GUARD_CAPACITY];
    for (unsigned i = 0; i < FILE_OBJECT_GUARD_CAPACITY; ++i) {
        file_object_owner_t owner = {(int32_t)(8 + i / 4), 7};
        tokens[i] = pin(key(12 + i), owner);
    }
    uint32_t rejected = 0;
    reist_file_object_key_t a = key(55);
    CHECK(file_object_guard_pin(&fixture, &a, service, client, snapshot(),
        10, &rejected) == -REIST_EMFILE);
    file_object_owner_t other = {20, 7};
    CHECK(file_object_guard_pin(&fixture, &a, service, other, snapshot(),
        10, &rejected) == -REIST_ENOSPC);
    CHECK(rejected == 0);
    CHECK(file_object_guard_revoke_media(&fixture, 1) == 0);
    CHECK(file_object_guard_verify(&fixture, tokens[0], service, client, 10) == -REIST_ESTALE);
    CHECK(fence_mask() == 0);
    CHECK(pin(key(12), client) != tokens[0]);
}

static void delegation_and_terminal_outcomes(void) {
    reset_fixture();
    reist_file_object_key_t a = key(12), b = key(13);
    uint32_t first = pin(a, client), mutation = 0, count = 0;
    file_object_owner_t recipient = {20, 11};
    uint32_t delegated = pin(a, recipient);
    CHECK(file_object_guard_count(&fixture, 1, &count, 10) == 0 && count == 2);
    CHECK(file_object_guard_release(&fixture, first, service, client) == 0);
    CHECK(file_object_guard_begin(&fixture, &a, 1, false, service,
        snapshot(), 10, 100, &mutation) == -REIST_EBUSY);
    file_object_owner_t wrong_service = {service.pid, service.generation + 1};
    CHECK(file_object_guard_verify(&fixture, delegated, wrong_service, recipient, 10) == -REIST_EACCES);
    CHECK(file_object_guard_cleanup(&fixture, wrong_service) == 0);
    CHECK(file_object_guard_verify(&fixture, delegated, service, recipient, 10) == 0);
    CHECK(file_object_guard_begin(&fixture, &b, 1, false, service,
        snapshot(), 10, 100, &mutation) == 0);
    CHECK(file_object_guard_end(&fixture, mutation, wrong_service,
        REIST_FILE_OBJECT_NO_EFFECT, 11) == -REIST_EACCES);
    CHECK(file_object_guard_end(&fixture, mutation, service, 0, 11) == -REIST_EINVAL);
    CHECK(file_object_guard_end(&fixture, mutation, service,
        REIST_FILE_OBJECT_UNKNOWN, 11) == 0);
    CHECK(fence_mask() == (1U << 1));
    CHECK(file_object_guard_verify(&fixture, delegated, service, recipient, 12) == -REIST_EIO);
    CHECK(file_object_guard_release(&fixture, delegated, service, recipient) == 0);
    CHECK(file_object_guard_revoke_media(&fixture, 1) == 0);
    CHECK(fence_mask() == (1U << 1)); /* media publication cannot erase uncertainty */
}

static void malformed_and_retirement(void) {
    reset_fixture();
    reist_file_object_key_t a = key(12), invalid = a;
    uint64_t epoch = snapshot();
    uint32_t token = 0x98765432U;
    invalid.reserved = 1;
    CHECK(file_object_guard_pin(&fixture, &invalid, service, client,
        epoch, 10, &token) == -REIST_EINVAL);
    invalid = a; invalid.resource = 32;
    CHECK(!file_object_guard_key_valid(&invalid));
    invalid = a; invalid.alias[0] = 1;
    CHECK(!file_object_guard_key_valid(&invalid));
    invalid = a; invalid.kind = REIST_FILE_OBJECT_FAT12;
    invalid.object_b = 480;
    CHECK(file_object_guard_key_valid(&invalid));
    invalid.object_b = 512;
    CHECK(!file_object_guard_key_valid(&invalid));
    invalid.object_b = 33;
    CHECK(!file_object_guard_key_valid(&invalid));
    invalid = a; invalid.kind = REIST_FILE_OBJECT_FAT32;
    memcpy(invalid.alias, "FOO     TXT", 11);
    CHECK(file_object_guard_key_valid(&invalid));
    invalid.alias[4] = 0;
    CHECK(!file_object_guard_key_valid(&invalid));
    reist_file_object_key_t pair[2] = {a, a}; pair[1].resource = 2;
    CHECK(file_object_guard_begin(&fixture, pair, 2, false, service,
        epoch, 10, 100, &token) == -REIST_EINVAL);
    CHECK(file_object_guard_begin(&fixture, &a, 3, false, service,
        epoch, 10, 100, &token) == -REIST_EINVAL);
    CHECK(file_object_guard_begin(&fixture, &a, 1, false, service,
        epoch, UINT64_MAX - 5, 10, &token) == -REIST_EINVAL);
    CHECK(token == 0x98765432U && snapshot() == epoch);
    /* Force the last usable generation in the actual redundant record. */
    token = pin(a, client);
    guard_pin_t state; size_t length = 0;
    CHECK(critical_object_read(&fixture.pins[0], REIST_FILE_OBJECT_VERSION,
        &state, sizeof(state), &length, guard_pin_valid) >= 0);
    state.generation = FILE_OBJECT_GUARD_GENERATION_MAX;
    CHECK(critical_object_update(&fixture.pins[0], REIST_FILE_OBJECT_VERSION,
        &state, sizeof(state), guard_pin_valid) == 0);
    token = (FILE_OBJECT_GUARD_GENERATION_MAX << 8) | 1;
    CHECK(file_object_guard_release(&fixture, token, service, client) == 0);
    uint32_t next = pin(a, client);
    CHECK((next & 255) == 2);
    CHECK(file_object_guard_verify(&fixture, token, service, client, 10) == -REIST_ESTALE);
    /* Reinitialization cannot recycle a pin after a corrupted BSS marker. */
    fixture.initialized = 0;
    CHECK(file_object_guard_init(&fixture) == -REIST_EIO);
    CHECK(fence_mask() == UINT32_MAX);
    reset_fixture();
    guard_control_t control;
    CHECK(guard_control_read(&fixture, &control) == 0);
    control.epoch = UINT64_MAX;
    CHECK(guard_control_write(&fixture, &control) == 0);
    CHECK(file_object_guard_begin(&fixture, &a, 1, false, service,
        UINT64_MAX, 10, 100, &token) == -REIST_EIO);
    CHECK(fence_mask() == UINT32_MAX);
}

/* Run two real native threads against the production CAS. A held mutation is
 * not ended until both threads return, so simultaneous success is forbidden.
 * Waits use OS synchronization with a fixed deadline, not a timing sleep. */
typedef struct {
    uint64_t epoch;
    uint32_t token;
    int result, mutation;
#ifdef _WIN32
    HANDLE start;
#else
    pthread_barrier_t *start;
#endif
} race_input_t;

#ifdef _WIN32
static DWORD WINAPI race_thread(void *opaque) {
#else
static void *race_thread(void *opaque) {
#endif
    race_input_t *input = opaque;
#ifdef _WIN32
    CHECK(WaitForSingleObject(input->start, 2000) == WAIT_OBJECT_0);
#else
    int arrived = pthread_barrier_wait(input->start);
    CHECK(arrived == 0 || arrived == PTHREAD_BARRIER_SERIAL_THREAD);
#endif
    reist_file_object_key_t a = key(12);
    input->result = input->mutation
        ? file_object_guard_begin(&fixture, &a, 1, false, service,
            input->epoch, 10, 100, &input->token)
        : file_object_guard_pin(&fixture, &a, service, client,
            input->epoch, 10, &input->token);
    return 0;
}

static void concurrent_admission(void) {
    for (unsigned round = 0; round < 64; ++round) {
        reset_fixture();
        race_input_t inputs[2] = {{0}, {0}};
        inputs[0].epoch = inputs[1].epoch = snapshot();
        inputs[1].mutation = 1;
#ifdef _WIN32
        HANDLE start = CreateEventW(NULL, TRUE, FALSE, NULL), threads[2];
        CHECK(start != NULL);
        for (unsigned i = 0; i < 2; ++i) {
            inputs[i].start = start;
            threads[i] = CreateThread(NULL, 0, race_thread, &inputs[i], 0, NULL);
            CHECK(threads[i] != NULL);
        }
        CHECK(SetEvent(start));
        CHECK(WaitForMultipleObjects(2, threads, TRUE, 3000) == WAIT_OBJECT_0);
        CHECK(CloseHandle(threads[0]) && CloseHandle(threads[1]) && CloseHandle(start));
#else
        pthread_barrier_t start; pthread_t threads[2];
        CHECK(pthread_barrier_init(&start, NULL, 3) == 0);
        for (unsigned i = 0; i < 2; ++i) {
            inputs[i].start = &start;
            CHECK(pthread_create(&threads[i], NULL, race_thread, &inputs[i]) == 0);
        }
        int arrived = pthread_barrier_wait(&start);
        CHECK(arrived == 0 || arrived == PTHREAD_BARRIER_SERIAL_THREAD);
        CHECK(pthread_join(threads[0], NULL) == 0 && pthread_join(threads[1], NULL) == 0);
        CHECK(pthread_barrier_destroy(&start) == 0);
#endif
        CHECK(inputs[0].result != 0 || inputs[1].result != 0);
        for (unsigned i = 0; i < 2; ++i) {
            CHECK(inputs[i].result == 0 || inputs[i].result == -REIST_EBUSY);
            CHECK((inputs[i].result == 0) == (inputs[i].token != 0));
        }
        CHECK(fence_mask() == 0);
    }
}

static void actual_fat_identity_projection(void) {
    /* Preserve the complete existing parser regression fixture as well as
     * proving that names/aliases produce one key with no second resolution. */
    CHECK(original_fat_fixture_main() == 0);
    static test_context_t context;
    const reist_vfs_shadow_io_t io = {&context, drive_info, read_sector};
    reist_vfs_shadow_object_t object, old;
    reist_file_object_key_t identity, alias;
    x86os_file_info_t info;
    initialize_fat32(&context);
    CHECK(reist_vfs_shadow_fat_object_open(&io, "/README.TXT", 11, &old, &info) == 0);
    uint32_t reads = context.reads;
    context.reads = 0;
    CHECK(reist_vfs_shadow_fat_object_open_key(&io, "/README.TXT", 11,
        &object, &info, &identity) == 0);
    CHECK(context.reads == reads && memcmp(&object, &old, sizeof(old)) == 0);
    CHECK(identity.kind == REIST_FILE_OBJECT_FAT32 && identity.resource == 0 &&
        identity.object_a == 2 && identity.object_b == 0 &&
        memcmp(identity.alias, "README  TXT", 11) == 0);
    CHECK(file_object_guard_key_valid(&identity));
    CHECK(reist_vfs_shadow_fat_object_open_key(&io, "/readme.txt", 11,
        &object, &info, &alias) == 0);
    CHECK(memcmp(&identity, &alias, sizeof(alias)) == 0);
    reset_fixture();
    uint32_t held = pin(identity, client), mutation = 0;
    CHECK(file_object_guard_begin(&fixture, &alias, 1, false, service,
        snapshot(), 10, 100, &mutation) == -REIST_EBUSY);
    CHECK(file_object_guard_release(&fixture, held, service, client) == 0);
    initialize_fat12(&context);
    CHECK(reist_vfs_shadow_fat_object_open_key(&io, "/Fat12 Long", 11,
        &object, &info, &identity) == 0);
    CHECK(identity.kind == REIST_FILE_OBJECT_FAT12 && identity.object_a == FAT12_ROOT_START &&
        identity.object_b == 96 && file_object_guard_key_valid(&identity));
    CHECK(reist_vfs_shadow_fat_object_open_key(&io, "/LONGFA~1.TXT", 13,
        &object, &info, &alias) == 0);
    CHECK(memcmp(&identity, &alias, sizeof(alias)) == 0);
    held = pin(identity, client);
    CHECK(file_object_guard_begin(&fixture, &alias, 1, false, service,
        snapshot(), 10, 100, &mutation) == -REIST_EBUSY);
    CHECK(file_object_guard_release(&fixture, held, service, client) == 0);
    CHECK(reist_vfs_shadow_fat_object_open_key(&io, "/missing", 8,
        &object, &info, &identity) == -REIST_ENOENT);
    reist_file_object_key_t zero = {0};
    CHECK(memcmp(&identity, &zero, sizeof(zero)) == 0);
}

static void fixed_request_validation(void) {
    CHECK(sizeof(reist_file_object_key_t) == 32);
    CHECK(sizeof(reist_file_object_guard_request_t) == 112);
    CHECK(offsetof(reist_file_object_guard_request_t, epoch) == 80);
    CHECK(offsetof(reist_file_object_guard_request_t, token) == 96);
    reist_file_object_guard_request_t request = {0};
    CHECK(!file_object_guard_request_valid(NULL));
    CHECK(!file_object_guard_request_valid(&request));
    request.version = REIST_FILE_OBJECT_VERSION;
    request.struct_size = sizeof(request);
    request.operation = REIST_FILE_OBJECT_SNAPSHOT;
    CHECK(file_object_guard_request_valid(&request));
    for (unsigned field = 0; field < 10; ++field) {
        reist_file_object_guard_request_t invalid = request;
        switch (field) {
        case 0: invalid.version++; break;
        case 1: invalid.struct_size--; break;
        case 2: invalid.reserved = 1; break;
        case 3: invalid.keys[1].object_b = 1; break;
        case 4: invalid.client_pid = 1; break;
        case 5: invalid.client_generation = 1; break;
        case 6: invalid.epoch = 1; break;
        case 7: invalid.deadline_ms = 1; break;
        case 8: invalid.token = 1; break;
        case 9: invalid.flags = 1; break;
        }
        CHECK(!file_object_guard_request_valid(&invalid));
    }
    request.operation = REIST_FILE_OBJECT_PIN;
    request.keys[0] = key(12);
    request.client_pid = client.pid;
    request.client_generation = client.generation;
    request.epoch = 1;
    CHECK(file_object_guard_request_valid(&request));
    request.flags = REIST_FILE_OBJECT_EXCLUSIVE;
    CHECK(!file_object_guard_request_valid(&request));
    request.flags = 0;
    request.keys[0] = (reist_file_object_key_t){0};
    request.operation = REIST_FILE_OBJECT_RELEASE;
    request.epoch = 0;
    request.token = 0x101;
    CHECK(file_object_guard_request_valid(&request));
    request.operation = REIST_FILE_OBJECT_VERIFY;
    CHECK(file_object_guard_request_valid(&request));
    request.token = 0x111;
    CHECK(!file_object_guard_request_valid(&request));
    request.operation = REIST_FILE_OBJECT_MUTATION_BEGIN;
    request.token = 0;
    request.client_pid = 0;
    request.client_generation = 0;
    request.epoch = 1;
    request.deadline_ms = 100;
    request.keys[0] = key(12);
    request.keys[1] = key(13);
    CHECK(file_object_guard_request_valid(&request));
    request.keys[1].resource = 2;
    CHECK(!file_object_guard_request_valid(&request));
    request.operation = REIST_FILE_OBJECT_MUTATION_END;
    request.keys[0] = request.keys[1] = (reist_file_object_key_t){0};
    request.epoch = request.deadline_ms = 0;
    request.token = 1;
    for (unsigned outcome = REIST_FILE_OBJECT_NO_EFFECT;
         outcome <= REIST_FILE_OBJECT_UNKNOWN; ++outcome) {
        request.flags = outcome;
        CHECK(file_object_guard_request_valid(&request));
    }
    request.flags++;
    CHECK(!file_object_guard_request_valid(&request));
}

int main(void) {
    admission_and_namespace();
    lifecycle_and_recovery();
    limits_expiry_and_integrity();
    quotas_and_media();
    delegation_and_terminal_outcomes();
    malformed_and_retirement();
    concurrent_admission();
    actual_fat_identity_projection();
    fixed_request_validation();
    puts("FILE_OBJECT_GUARD_CORE_OK admission namespace lifecycle quota expiry corruption media delegation retirement threads=128 fat12 fat32 schema");
    return 0;
}
#endif
