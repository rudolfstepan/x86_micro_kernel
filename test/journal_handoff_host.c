#include <stdio.h>
#include <string.h>
#include "include/kernel/file_object_guard.h"
#include "userspace/storage/include/reist/fat32_transaction.h"

#if defined(JOURNAL_HANDOFF_TRACE_TEST)
#define MAX_TASKS 32
#define REQUIRE(x) do { if (!(x)) { printf("TRACE FAIL %u: %s\n", (unsigned)__LINE__, #x); return 1; } } while (0)
typedef struct { int task_id, pid; uint32_t generation; } Process;
typedef struct { int value; } wait_queue_t;
static Process parent_record = {3, 6, 7}, *parent = &parent_record;
static wait_queue_t queue;
static int result_value, blocked_result, waits, reaps, copies, inspections;
static bool pointer_valid = true, have_queue = true;
static Process *scheduler_current_process(void) { return parent; }
static int paging_current_directory(void) { return 1; }
static bool user_range_accessible(int directory, uint32_t address, size_t size, bool write) {
    (void)directory; (void)address; (void)size; (void)write; return pointer_valid;
}
static uint32_t process_table_lock_irqsave(void) { return 0; }
static void process_table_unlock_irqrestore(uint32_t flags) { (void)flags; }
static int process_table_lock_ref(void) { return 2; }
static int process_wait_status_locked(Process *owner, int pid, int *status, wait_queue_t **out) {
    (void)owner; (void)pid; ++inspections; *status = 42; *out = have_queue ? &queue : NULL;
    return result_value;
}
static int scheduler_reap_finished_tasks(void) { ++reaps; return 0; }
static int copy_to_user(void *out, const void *in, size_t size) { ++copies; memcpy(out, in, size); return 0; }
#define TASK_BLOCK_WAITING 4
static int wait_queue_block_until_spinlocked(wait_queue_t *q, int reason, uint64_t deadline, int lock, uint32_t flags) {
    (void)flags;
    if (q != &queue || reason != TASK_BLOCK_WAITING || deadline != UINT64_MAX || lock != 2) return -1;
    ++waits; result_value = 1; return blocked_result;
}
#define SERIAL_COM1 0x3f8U
#define SERIAL_DATA(p) (p)
#define SERIAL_TX_POLL_LIMIT 100000U
static bool serial_com1_present = true, tx_ready = true;
static unsigned polls, writes;
static bool serial_is_transmit_empty(uint16_t port) { (void)port; ++polls; return tx_ready; }
static void outb(uint16_t port, char ch) { (void)port; (void)ch; ++writes; }
#include "handoff_trace.inc"
int main(void) {
    int status = 0;
    REQUIRE(syscall_wait(10, &status) == 10 && status == 42);
    REQUIRE(waits == 1 && reaps == 1 && copies == 1 && inspections == 2);
    REQUIRE(r341_wait_trace[3][0] == 6 && r341_wait_trace[3][1] == 7 &&
            r341_wait_trace[3][2] == 10 && r341_wait_trace[3][3] == 6);
    result_value = -1; REQUIRE(syscall_wait(11, &status) == -10 && r341_wait_trace[3][3] == 4);
    result_value = 0; have_queue = false;
    REQUIRE(syscall_wait(11, &status) == -11 && r341_wait_trace[3][3] == 7);
    have_queue = true; blocked_result = -1;
    REQUIRE(syscall_wait(11, &status) == -11 && r341_wait_trace[3][3] == 8);
    pointer_valid = false; REQUIRE(syscall_wait(11, &status) == -14);
    parent = NULL; REQUIRE(syscall_wait(11, &status) == -14);
    REQUIRE(reaps == 1 && copies == 1 && waits == 2 && inspections == 5);
    r341_exit_mark(-1, 99, 1, 0, 1); r341_exit_mark(MAX_TASKS, 99, 1, 0, 1);
    for (int i = 0; i < MAX_TASKS; ++i) REQUIRE(r341_exit_trace[i][4] == 0);
    r341_exit_mark(3, 10, 1, 0, 1); r341_exit_mark(3, 10, 2, 0, 17);
    REQUIRE(r341_exit_trace[3][0] == 10 && r341_exit_trace[3][1] == 2 &&
            r341_exit_trace[3][3] == 17 && r341_exit_trace[3][4] == 2);
    serial_write_char(SERIAL_COM1, 'a');
    REQUIRE(polls == 1 && writes == 1 && r341_serial_trace[0] == 1 && r341_serial_trace[1] == 1);
    serial_com1_present = false; serial_write_char(SERIAL_COM1, 'b');
    REQUIRE(polls == 1 && writes == 1 && r341_serial_trace[2] == 1 && r341_serial_trace[6] == 'b');
    serial_com1_present = true; tx_ready = false; serial_write_char(SERIAL_COM1, 'c');
    REQUIRE(polls == 100001 && writes == 1 && r341_serial_trace[3] == 1 && r341_serial_trace[7] == 'c');
    REQUIRE(r341_serial_trace[0] == 3 && r341_serial_trace[4] == 'c' && r341_serial_trace[8] == SERIAL_COM1);
    r341_serial_trace[0] = UINT32_MAX; r341_serial_mark(0, SERIAL_COM1, 'd');
    REQUIRE(r341_serial_trace[0] == 0);
    r341_serial_mark(4, 0, 0); REQUIRE(r341_serial_trace[4] == 'd');
    puts("PRIVATE_TRACE PASS: actual wait outcomes/serial polling unchanged; bounded generation records");
    return 0;
}
#elif defined(JOURNAL_HANDOFF_FIXTURE_TEST)
static reist_fat32_transaction_t handoff_transaction;
static int handoff_raw_error, raw_reply;
static unsigned raw_calls;
static uint8_t raw_buffer[512];
static bool raw_request_valid = true;
static int x86os_storage_journal_io(const reist_storage_journal_request_t* request, void* data) {
    ++raw_calls;
    raw_request_valid = raw_request_valid && request->version == 1 &&
        request->struct_size == sizeof(*request) && request->token == 19 &&
        request->resource == 1 && request->sector == 60000 && request->count == 1 &&
        request->operation == REIST_STORAGE_JOURNAL_READ && request->reserved == 0 &&
        data == raw_buffer;
    return raw_reply;
}
#include "handoff_private.inc"
int main(void) {
    handoff_transaction.token = 19;
    const int replies[] = {0, -5, 0, -14};
    const int retained[] = {0, -5, -5, -5};
    for (unsigned i = 0; i < 4; ++i) {
        raw_reply = replies[i];
        if (handoff_raw(REIST_STORAGE_JOURNAL_READ, 60000, 1, raw_buffer) != replies[i] ||
            handoff_raw_error != retained[i]) return 1;
    }
    handoff_raw_error = 0;
    raw_reply = -16;
    if (handoff_raw(REIST_STORAGE_JOURNAL_READ, 60000, 1, raw_buffer) != -16 ||
        handoff_raw_error != -16 || !raw_request_valid || raw_calls != 5) return 1;
    puts("PRIVATE_HANDOFF_ERRNO PASS 5 exact calls; first failure retained");
    return 0;
}
#elif defined(JOURNAL_HANDOFF_PIO_TEST)
#include "drivers/block/ata.h"
#include <stdlib.h>
#define REQUIRE(x) do { if (!(x)) { printf("PIO FAIL %u: %s\n", (unsigned)__LINE__, #x); return 1; } } while (0)
#define ATA_WAIT_TIMEOUT_MS 500U
#define ATA_POLL_DELAY_MS 1U
#define ATA_STATUS_DRQ 8U
drive_t detected_drives[MAX_DRIVES];
typedef struct { uint16_t base; uint32_t lba; bool is_master, valid; uint8_t data[512]; } ata_cache_entry_t;
static ata_cache_entry_t cache[32];
static uint32_t consecutive_read_failures;
static uint64_t pio_now, pio_deadline;
static unsigned commands, writes, reads, flushes, late, sleeps, selected, count_reg, remaining;
static unsigned expire_select, expire_command, expire_write, expire_read;
static bool expire_register;
static uint8_t command;
static bool busy;
static uint64_t pit_monotonic_ms(void) { return pio_now; }
static int scheduler_current_task_id(void) { return 1; }
static bool scheduler_can_sleep(void) { return true; }
static bool irq_enabled(void) { return true; }
static bool irq_in_context(void) { return false; }
static int scheduler_sleep_ms(uint32_t ms) { ++sleeps; pio_now += ms; return 0; }
static void pit_delay(uint32_t ms) { ++sleeps; pio_now += ms; }
uint16_t ata_control_port_for_base(uint16_t base) { return base+0x206; }
static uint8_t inb(uint16_t port) {
    (void)port;
    if (busy) return 0x80;
    return command == ATA_IDENTIFY || remaining ? 0x48 : 0x40;
}
static void outb(uint16_t port, uint8_t value) {
    if (expire_register && port == ATA_LBA_HIGH(0x1f0)) pio_now = pio_deadline;
    if (port == ATA_DRIVE_HEAD(0x1f0)) {
        ++selected;
        if (expire_select && selected == expire_select) pio_now = pio_deadline;
    }
    if (port == ATA_SECTOR_CNT(0x1f0)) count_reg = value ? value : 256;
    if (port == ATA_COMMAND(0x1f0)) {
        if (pio_deadline && pio_now >= pio_deadline) ++late;
        ++commands; command = value;
        if (value == ATA_WRITE_SECTORS || value == ATA_WRITE_SECTORS_EXT ||
            value == ATA_READ_SECTORS || value == ATA_READ_MULTIPLE ||
            value == ATA_READ_SECTORS_EXT || value == ATA_READ_MULTIPLE_EXT) remaining = count_reg;
        else remaining = 0;
        if (value == ATA_FLUSH_CACHE || value == ATA_FLUSH_CACHE_EXT) ++flushes;
        if (expire_command && commands == expire_command) busy = true;
    }
}
static void ata_selection_delay(uint16_t base) { (void)base; }
static void outsw(uint16_t port, const void* data, unsigned words) {
    (void)port; (void)data;
    if (!remaining || words != 256) abort();
    if (pio_deadline && pio_now >= pio_deadline) ++late;
    --remaining; ++writes;
    if (expire_write && writes == expire_write) pio_now = pio_deadline;
}
static void insw(uint16_t port, void* data, unsigned words) {
    (void)port;
    if (pio_deadline && pio_now >= pio_deadline) ++late;
    memset(data, 0, words*2);
    if (command == ATA_IDENTIFY) {
        uint16_t* identify = data;
        identify[0] = 0x40; identify[47] = 0x8010; identify[59] = 0x110;
        command = 0;
    } else {
        if (!words || words/256 > remaining) abort();
        remaining -= words/256; reads += words/256;
        if (expire_read && reads >= expire_read) pio_now = pio_deadline;
    }
}
static int ata_resource_index(uint16_t base, bool master) { return base == 0x1f0 && master ? 0 : -1; }
static ata_cache_entry_t* ata_cache_slot(uint16_t base, uint32_t lba, bool master) {
    (void)base; (void)master; return &cache[lba%32];
}
static bool ata_select_target(uint16_t base, uint8_t head, uint32_t timeout) {
    (void)timeout; outb(ATA_DRIVE_HEAD(base), head); return !busy;
}
static bool wait_for_drive_data_ready(uint16_t base, uint32_t timeout) {
    (void)base; (void)timeout; return !busy && remaining != 0;
}
static bool wait_for_drive_ready(uint16_t base, uint32_t timeout) {
    (void)base; (void)timeout; return !busy && remaining == 0;
}
static bool ata_flush_failure(uint16_t base, bool master, uint8_t cmd, uint8_t status, const char* why) {
    (void)base; (void)master; (void)cmd; (void)status; (void)why; return false;
}
static bool ata_wait_flush_complete(uint16_t base, bool master, uint8_t cmd, uint32_t timeout) {
    (void)base; (void)master; (void)cmd; (void)timeout; return !busy;
}
#include "ata_pio.inc"
static void pio_reset(void) {
    pio_now = 10; pio_deadline = 15;
    commands = writes = reads = flushes = late = sleeps = selected = count_reg = remaining = 0;
    expire_select = expire_command = expire_write = expire_read = 0;
    expire_register = false;
    command = 0; busy = false;
    memset(cache, 0, sizeof(cache));
    detected_drives[0] = (drive_t){.type=DRIVE_TYPE_ATA, .sectors=UINT32_MAX,
        .base=0x1f0, .is_master=true, .lba48_supported=true, .flush_cache_supported=true};
}
int main(void) {
    static uint8_t data[256*512];
    for (unsigned extended = 0; extended < 2; ++extended) {
        uint32_t sector = extended ? ATA_LBA28_LIMIT : 1;
        for (unsigned mode = 0; mode < 6; ++mode) {
            pio_reset();
            if (mode == 1) expire_select = 1;
            if (mode == 2) expire_command = 1;
            if (mode == 3) expire_write = 1;
            if (mode == 4) pio_now = pio_deadline;
            if (mode == 5) expire_register = true;
            bool ok = ata_write_sectors_pio_deferred_until(0x1f0, sector, 20, data, true, pio_deadline);
            REQUIRE(ok == (mode == 0) && !late && !flushes);
            REQUIRE(writes == (mode == 0 ? 20U : mode == 3 ? 1U : 0U));
            REQUIRE(commands == (mode == 1 || mode == 4 || mode == 5 ? 0U : 1U));
        }
        for (unsigned mode = 0; mode < 5; ++mode) {
            pio_reset();
            if (mode == 1) expire_select = 1;
            if (mode == 2) expire_command = 1;
            if (mode == 3) expire_read = 16;
            if (mode == 4) pio_now = pio_deadline;
            bool ok = ata_read_sectors_pio_until(0x1f0, sector, 128, data, true, pio_deadline);
            REQUIRE(ok == (mode == 0) && !late && !writes && !flushes);
            REQUIRE(reads == (mode == 0 ? 128U : mode == 3 ? 16U : 0U));
            if (mode) for (unsigned i = 0; i < 32; ++i) REQUIRE(!cache[i].valid);
        }
        for (unsigned mode = 0; mode < 4; ++mode) {
            pio_reset();
            detected_drives[0].flush_cache_ext_supported = extended != 0;
            if (mode == 1) expire_select = 1;
            if (mode == 2) expire_command = 1;
            if (mode == 3) pio_now = pio_deadline;
            bool ok = ata_flush_cache_until(0x1f0, true, &detected_drives[0], pio_deadline);
            REQUIRE(ok == (mode == 0) && !late);
            REQUIRE(flushes == (mode == 1 || mode == 3 ? 0U : 1U));
        }
        pio_reset(); pio_deadline = 0;
        REQUIRE(ata_write_sectors_pio_deferred_impl(0x1f0, sector, 20, data, true));
        REQUIRE(writes == 20 && commands == 1 && !flushes && !sleeps);
        REQUIRE(ata_flush_cache_impl(0x1f0, true, &detected_drives[0]));
        REQUIRE(flushes == 1 && commands == 2);
        REQUIRE(ata_read_sectors_pio_impl(0x1f0, sector, 128, data, true));
        REQUIRE(reads == 128 && commands == 4 && !sleeps);
    }
    puts("JOURNAL_PIO_DEADLINE_OK LBA28/48 selection readiness read write flush no-late-command legacy-bulk");
    return 0;
}
#elif defined(JOURNAL_HANDOFF_ATA_TEST)
#include "drivers/bus/drives.h"
#include "drivers/block/ata.h"
#include <stdlib.h>
#define REQUIRE(x) do { if (!(x)) { printf("ATA FAIL %u: %s\n", (unsigned)__LINE__, #x); return 1; } } while (0)
drive_t detected_drives[MAX_DRIVES];
short drive_count = 3;
static ata_undo_journal_t ata_journal;
static bool ata_write_fenced, held, supervised, fail_write;
static unsigned begins, ends, barriers, readbacks;
static uint8_t media[512][512];
static uint64_t ata_now = 10, after_lock, after_write, expected_deadline = 510;
static unsigned late_effects, writes;
#define KASSERT_NOT_IRQ() ((void)0)
#define ATA_TRANSACTION_LOCK_TIMEOUT_MS 10000U
static int ata_transaction_mutex;
static int kernel_mutex_lock_until(int* mutex, uint64_t deadline) {
    if (mutex != &ata_transaction_mutex || held || deadline != expected_deadline) abort();
    held = true;
    if (after_lock) ata_now = after_lock;
    return 0; /* Even a resumed successful acquisition must be rechecked. */
}
static void ata_transaction_end(void) { if (!held) abort(); held = false; }
static void ata_journal_ensure_initialized(void) {}
static int ata_resource_index(unsigned short base, bool master) {
    (void)master; return base == 0x1f0 ? 1 : -1;
}
static drive_t* ata_compat_partition_drive(unsigned short base) {
    return base == 0xb000 ? &detected_drives[0] : NULL;
}
static drive_t* ata_partition_translate(drive_t* partition, uint32_t lba, uint32_t* absolute) {
    if (lba >= partition->sectors) return NULL;
    *absolute = lba + partition->lba_offset;
    return &detected_drives[partition->parent_resource];
}
static bool storage_writes_fenced(void) { return false; }
#include "ata_bounds.inc"
static uint64_t pit_monotonic_ms(void) { return ata_now; }
static bool storage_write_begin(uint32_t resource, uint64_t now) {
    (void)resource; (void)now;
    if (!held || supervised) abort();
    supervised = true; ++begins; return true;
}
static bool storage_write_end(bool durable) {
    if (!held || !supervised) abort();
    supervised = false; ++ends; return durable;
}
static bool ata_write_sectors_pio_deferred_until(unsigned short base, uint32_t lba,
    uint32_t count, const void* data, bool master, uint64_t deadline) {
    (void)base; (void)master;
    if (!held || !supervised) abort();
    if (!ata_pio_range_valid(&detected_drives[1], lba, count)) abort();
    if (deadline != expected_deadline) abort();
    if (ata_now >= deadline) ++late_effects;
    if (fail_write) return false;
    writes += count;
    memcpy(media[lba], data, count*512U);
    if (after_write) ata_now = after_write;
    return true;
}
static bool ata_read_sectors_pio_until(unsigned short base, uint32_t lba, uint32_t count,
    void* data, bool master, uint64_t deadline) {
    (void)base; (void)master;
    if (!held) abort();
    if (!ata_pio_read_range_valid(&detected_drives[1], lba, count)) abort();
    if (deadline != expected_deadline) abort();
    if (ata_now >= deadline) ++late_effects;
    readbacks += count; memcpy(data, media[lba], count*512); return true;
}
static bool ata_flush_cache_until(unsigned short base, bool master, const drive_t* drive, uint64_t deadline) {
    (void)base; (void)master;
    if (drive != &detected_drives[1] || deadline != expected_deadline) abort();
    if (ata_now >= deadline) ++late_effects;
    if (!held || !supervised) abort();
    ++barriers; return true;
}
#include "ata_handoff.inc"
static int deadline_cases(void) {
    for (unsigned duration = 1; duration <= 5000; duration += 4999) {
        for (unsigned mode = 0; mode < 5; ++mode) {
            held = supervised = fail_write = false;
            begins = ends = barriers = readbacks = writes = late_effects = 0;
            ata_now = 10; expected_deadline = ata_now + duration;
            after_lock = mode == 1 || mode == 3 || mode == 4 ? expected_deadline : 0;
            after_write = mode == 2 ? expected_deadline : 0;
            static file_object_guard_t guard;
            memset(&guard, 0, sizeof(guard)); /* Independent fresh fixture. */
            file_object_owner_t owner = {5, 3};
            reist_file_object_key_t key = {.kind=REIST_FILE_OBJECT_FAT32, .resource=1, .object_a=2};
            memcpy(key.alias, "TARGET  TXT", 11);
            uint64_t epoch = 0, deadline = 0;
            uint32_t token = 0, fence = 0;
            REQUIRE(!file_object_guard_init(&guard));
            REQUIRE(!file_object_guard_snapshot(&guard, &epoch, ata_now));
            REQUIRE(!file_object_guard_begin_mode(&guard, &key, 1,
                REIST_FILE_OBJECT_EXCLUSIVE | REIST_FILE_OBJECT_EXTERNAL_JOURNAL,
                owner, epoch, ata_now, expected_deadline, &token));
            bool pending = false;
            REQUIRE(!file_object_guard_journal_io_deadline(&guard, owner, token, 1,
                FILE_OBJECT_JOURNAL_CHECK, ata_now, &pending, &deadline));
            REQUIRE(deadline == expected_deadline && !pending);
            uint32_t operation = mode == 3 ? REIST_STORAGE_JOURNAL_READ :
                mode == 4 ? REIST_STORAGE_JOURNAL_FLUSH : REIST_STORAGE_JOURNAL_WRITE_DEFERRED;
            if (operation == REIST_STORAGE_JOURNAL_WRITE_DEFERRED)
                REQUIRE(!file_object_guard_journal_io(&guard, owner, token, 1,
                    FILE_OBJECT_JOURNAL_WRITE, ata_now, NULL));
            static uint8_t data[256*512];
            memset(data, 0x5a, sizeof(data));
            int result = ata_external_journal_io(1, operation, mode == 4 ? 0 : 1,
                mode == 4 ? 0 : 256, mode == 4 ? NULL : data, pending, deadline);
            int completion = file_object_guard_journal_io(&guard, owner, token, 1,
                FILE_OBJECT_JOURNAL_CHECK, ata_now, NULL);
            REQUIRE(!file_object_guard_fenced(&guard, &fence) && !held && !late_effects);
            if (!mode) REQUIRE(!result && !completion && !fence && writes == 256 && readbacks == 256);
            else REQUIRE(result < 0 && completion < 0 && (fence & 2) && !supervised);
            if (mode == 2) REQUIRE(writes == 20 && readbacks == 0 && ends == 1);
            if (mode == 1 || mode == 3 || mode == 4) REQUIRE(!writes && !readbacks && !barriers);
        }
    }
    puts("JOURNAL_DEADLINE_OK actual-guard lock batch readback read flush duration=1/5000");
    return 0;
}
int main(void) {
    detected_drives[1].base = 0x1f0; detected_drives[1].type = DRIVE_TYPE_ATA;
    detected_drives[1].sectors = 512; detected_drives[1].is_master = true;
    detected_drives[0] = detected_drives[1]; detected_drives[0].base = 0xb000;
    detected_drives[0].type = DRIVE_TYPE_PARTITION; detected_drives[0].parent_resource = 1;
    detected_drives[0].lba_offset = 32; detected_drives[0].sectors = 400;
    detected_drives[2].type = DRIVE_TYPE_AHCI; detected_drives[2].sectors = 512;
    ata_journal.enabled = true; ata_journal.transaction_depth = 1;
    REQUIRE(ata_external_journal_handoff(0x1f0, true, expected_deadline) == -REIST_EBUSY && ata_journal.enabled && !held);
    ata_journal.transaction_depth = 0; ata_journal.entry_count = 1;
    REQUIRE(ata_external_journal_handoff(0x1f0, true, expected_deadline) == -REIST_EBUSY && ata_journal.entry_count == 1);
    ata_journal.entry_count = 0;
    REQUIRE(!ata_external_journal_handoff(0xb000, true, expected_deadline) && !ata_journal.enabled && !held);
    static uint8_t data[256*512]; memset(data, 0xa5, sizeof(data));
    REQUIRE(!ata_external_journal_io(0, REIST_STORAGE_JOURNAL_WRITE_DEFERRED, 1, 256, data, false, expected_deadline));
    REQUIRE(!held && supervised && begins == 1 && ends == 0 && barriers == 0 && readbacks == 256);
    REQUIRE(media[33][0] == 0xa5 && media[32][0] == 0 && media[289][0] == 0);
    REQUIRE(!ata_external_journal_io(0, REIST_STORAGE_JOURNAL_WRITE_DEFERRED, 1, 256, data, true, expected_deadline));
    REQUIRE(begins == 1 && ends == 0 && !held);
    REQUIRE(!ata_external_journal_io(0, REIST_STORAGE_JOURNAL_FLUSH, 0, 0, NULL, true, expected_deadline));
    REQUIRE(!held && !supervised && ends == 1 && barriers == 1);
    REQUIRE(ata_external_journal_io(0, REIST_STORAGE_JOURNAL_READ, 399, 2, data, false, expected_deadline) == -REIST_EINVAL);
    REQUIRE(ata_external_journal_io(2, REIST_STORAGE_JOURNAL_READ, 1, 1, data, false, expected_deadline) == -REIST_ENOTSUP);
    REQUIRE(ata_external_journal_io(1, REIST_STORAGE_JOURNAL_WRITE_DEFERRED, 1, 257, data, false, expected_deadline) == -REIST_EINVAL);
    fail_write = true;
    REQUIRE(ata_external_journal_io(1, REIST_STORAGE_JOURNAL_WRITE_DEFERRED, 1, 1, data, false, expected_deadline) == -REIST_EIO);
    REQUIRE(!held && !supervised && begins == 2 && ends == 2 && barriers == 1);
    puts("JOURNAL_HANDOFF_ATA_OK pending-retained full-readback partition-once no-implicit-flush bounded-supervision");
    return deadline_cases();
}
#else

static unsigned checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    printf("FAIL line %u: %s\n", (unsigned)__LINE__, #condition); } } while (0)

static reist_file_object_key_t fat_key(void) {
    reist_file_object_key_t key = {0};
    key.kind = REIST_FILE_OBJECT_FAT32;
    key.resource = 1;
    key.object_a = 2;
    memcpy(key.alias, "TARGET  TXT", 11);
    return key;
}

static void request_validation(void) {
    reist_file_object_guard_request_t request = {0};
    request.version = REIST_FILE_OBJECT_VERSION;
    request.struct_size = sizeof(request);
    request.operation = REIST_FILE_OBJECT_MUTATION_BEGIN;
    request.keys[0] = fat_key();
    request.epoch = 1;
    request.deadline_ms = 1000;
    /* Numeric append-only flag makes the missing-mode regression build
     * against the accepted baseline as well as the implementation. */
    request.flags = REIST_FILE_OBJECT_EXCLUSIVE | 2U;
    CHECK(file_object_guard_request_valid(&request));
    request.flags = 2U;
    CHECK(!file_object_guard_request_valid(&request));
    request.flags = REIST_FILE_OBJECT_EXCLUSIVE | 2U;
    request.keys[1] = request.keys[0];
    CHECK(!file_object_guard_request_valid(&request));
    reist_storage_journal_request_t io = {1, sizeof(io), REIST_STORAGE_JOURNAL_READ, 1, 1, 0, 256, 0};
    CHECK(sizeof(io) == 32 && file_object_guard_journal_request_valid(&io));
    io.count = 257; CHECK(!file_object_guard_journal_request_valid(&io));
    io.count = 0; CHECK(!file_object_guard_journal_request_valid(&io));
    io.count = 2; io.sector = UINT32_MAX; CHECK(!file_object_guard_journal_request_valid(&io));
    io.count = 1; CHECK(file_object_guard_journal_request_valid(&io));
    io.operation = REIST_STORAGE_JOURNAL_FLUSH;
    CHECK(!file_object_guard_journal_request_valid(&io));
    io.count = io.sector = 0; CHECK(file_object_guard_journal_request_valid(&io));
    io.reserved = 1; CHECK(!file_object_guard_journal_request_valid(&io));
}

static file_object_guard_t guard;
static const file_object_owner_t owner = {9, 7};
static uint64_t now;
static unsigned transfers, effects, flushes, writes, cut, write_through;
static uint8_t disk[512][512], stable[512][512], original[512][512];
static uint8_t payload[512], readback[512];
static reist_fat32_transaction_t transaction;

static int guard_call(void* context, reist_file_object_guard_request_t* request) {
    (void)context;
    CHECK(file_object_guard_request_valid(request));
    if (request->operation == REIST_FILE_OBJECT_SNAPSHOT)
        return file_object_guard_snapshot(&guard, &request->epoch, now);
    if (request->operation == REIST_FILE_OBJECT_MUTATION_BEGIN)
        return file_object_guard_begin_mode(&guard, request->keys, 1, request->flags,
            owner, request->epoch, now, request->deadline_ms, &request->token);
    CHECK(request->operation == REIST_FILE_OBJECT_MUTATION_END);
    return file_object_guard_end(&guard, request->token, owner, request->flags, now);
}

static int transfer(void* context, const reist_storage_journal_request_t* request, void* data) {
    (void)context;
    CHECK(!guard.lock && !guard.control.publication_lock);
    CHECK(file_object_guard_journal_request_valid(request));
    bool pending;
    int result = file_object_guard_journal_io(&guard, owner, request->token,
        request->resource, FILE_OBJECT_JOURNAL_CHECK, now, &pending);
    if (result) return result;
    ++transfers;
    if (request->operation == REIST_STORAGE_JOURNAL_FLUSH) {
        ++flushes;
        if (++effects == cut) return -REIST_EIO;
        memcpy(stable, disk, sizeof(disk));
        return file_object_guard_journal_io(&guard, owner, request->token, request->resource,
            FILE_OBJECT_JOURNAL_FLUSHED, now, NULL);
    }
    CHECK(data && request->sector < 512 && request->count <= 512 - request->sector);
    if (request->operation == REIST_STORAGE_JOURNAL_READ) {
        memcpy(data, disk[request->sector], request->count * 512U);
        return 0;
    }
    result = file_object_guard_journal_io(&guard, owner, request->token, request->resource,
        FILE_OBJECT_JOURNAL_WRITE, now, NULL);
    if (result) return result;
    ++writes;
    for (unsigned i = 0; i < request->count; ++i) {
        if (++effects == cut) return -REIST_EIO;
        memcpy(disk[request->sector+i], (uint8_t*)data+i*512, 512);
        if (write_through) memcpy(stable[request->sector+i], disk[request->sector+i], 512);
    }
    return 0;
}

static const reist_fat32_transaction_io_t io = {NULL, guard_call, transfer};
static void reset_guard(void) {
    memset(&guard, 0, sizeof(guard));
    memset(&transaction, 0, sizeof(transaction));
    CHECK(!file_object_guard_init(&guard));
    now = 1;
    transfers = effects = flushes = writes = cut = 0;
}
static void reset_disk(void) {
    reset_guard();
    memset(disk, 0, sizeof(disk));
    ata_journal_record_t record;
    ata_undo_journal_make_clean(&record, 7);
    memcpy(disk[8], &record, sizeof(record));
    memcpy(disk[31], &record, sizeof(record));
    for (unsigned i = 0; i < 20; ++i) memset(disk[64+i], 0x11+i, 512);
    memcpy(stable, disk, sizeof(disk));
    memcpy(original, disk, sizeof(disk));
}
static int begin(void) {
    reist_file_object_key_t key = fat_key();
    return reist_fat32_transaction_begin(&transaction, &io, &key, 0, 512, 32, 5000);
}

static void guard_authority(void) {
    reset_disk();
    CHECK(!begin());
    uint32_t token = transaction.token, outcome = 0;
    CHECK(file_object_guard_mutation_authorized(&guard, owner, 1, now) == -REIST_EBUSY);
    CHECK(file_object_guard_legacy_enter(&guard, now) == -REIST_EBUSY);
    CHECK(file_object_guard_can_open(&guard, 1, now) == -REIST_EBUSY);
    CHECK(!file_object_guard_can_open(&guard, 0, now));
    CHECK(file_object_guard_journal_io(&guard, (file_object_owner_t){9,8}, token, 1, 0, now, NULL) == -REIST_EACCES);
    CHECK(file_object_guard_journal_io(&guard, owner, token+1, 1, 0, now, NULL) == -REIST_ESTALE);
    CHECK(file_object_guard_journal_io(&guard, owner, token, 0, 0, now, NULL) == -REIST_EACCES);
    CHECK(!reist_fat32_transaction_finish(&transaction, false, &outcome));
    CHECK(outcome == REIST_FILE_OBJECT_NO_EFFECT && !effects);
    CHECK(!file_object_guard_legacy_enter(&guard, now));
    CHECK(file_object_guard_journal_io(&guard, owner, token, 1, 0, now, NULL) == -REIST_ESTALE);
    CHECK(!begin()); token = transaction.token;
    CHECK(!file_object_guard_journal_io(&guard, owner, token, 1, FILE_OBJECT_JOURNAL_WRITE, now, NULL));
    CHECK(file_object_guard_end(&guard, token, owner, REIST_FILE_OBJECT_NO_EFFECT, now) == -REIST_EINVAL);
    CHECK(file_object_guard_end(&guard, token, owner, REIST_FILE_OBJECT_DURABLE_COMMIT, now) == -REIST_EINVAL);
    CHECK(file_object_guard_cancel_undelivered(&guard, REIST_FILE_OBJECT_MUTATION_BEGIN,
                                              token, owner, (file_object_owner_t){0}) == -REIST_EINVAL);
    CHECK(!file_object_guard_journal_io(&guard, owner, token, 1, FILE_OBJECT_JOURNAL_FLUSHED, now, NULL));
    CHECK(file_object_guard_end(&guard, token, owner, REIST_FILE_OBJECT_NO_EFFECT, now) == -REIST_EINVAL);
    CHECK(!file_object_guard_end(&guard, token, owner, REIST_FILE_OBJECT_DURABLE_COMMIT, now));
    reset_guard(); CHECK(!begin()); now = 5000;
    CHECK(file_object_guard_journal_io(&guard, owner, transaction.token, 1, 0, now, NULL) == -REIST_EIO);
    CHECK(!file_object_guard_cleanup(&guard, owner));
    CHECK(file_object_guard_can_open(&guard, 1, now) == -REIST_EIO);
    CHECK(!file_object_guard_can_open(&guard, 0, now));
}

static void transactions(void) {
    reset_disk();
    CHECK(!begin());
    for (unsigned i = 0; i < 20; ++i) {
        memset(payload, 0x70+i, sizeof(payload));
        CHECK(!reist_fat32_transaction_stage(&transaction, 64+i, payload));
        CHECK(!reist_fat32_transaction_read(&transaction, 64+i, readback));
        CHECK(!memcmp(payload, readback, 512));
    }
    CHECK(!effects && !memcmp(disk, original, sizeof(disk)));
    uint32_t outcome;
    CHECK(!reist_fat32_transaction_finish(&transaction, true, &outcome));
    CHECK(outcome == REIST_FILE_OBJECT_DURABLE_COMMIT && flushes == 4 && writes == 6);
    CHECK(effects == 48); /*20 undo +2 ACTIVE +20 targets +2 CLEAN +4 barriers */
    CHECK(!memcmp(disk, stable, sizeof(disk)));
    for (unsigned i = 0; i < 20; ++i) CHECK(disk[64+i][0] == 0x70+i);
    reset_disk(); CHECK(!begin());
    for (unsigned i = 0; i < 20; ++i) CHECK(!reist_fat32_transaction_stage(&transaction, 64+i, payload));
    CHECK(reist_fat32_transaction_stage(&transaction, 84, payload) < 0);
    CHECK(reist_fat32_transaction_finish(&transaction, true, &outcome) < 0);
    CHECK(outcome == REIST_FILE_OBJECT_NO_EFFECT && !effects);
    CHECK(!memcmp(disk, original, sizeof(disk)));
}

static void interruption_campaign(void) {
    for (write_through = 0; write_through <= 1; ++write_through) {
        for (unsigned stop = 1; stop <= 49; ++stop) {
            reset_disk(); CHECK(!begin());
            for (unsigned i = 0; i < 20; ++i) {
                memset(payload, 0x70+i, 512);
                CHECK(!reist_fat32_transaction_stage(&transaction, 64+i, payload));
            }
            cut = stop;
            uint32_t outcome;
            int result = reist_fat32_transaction_finish(&transaction, true, &outcome);
            if (stop <= 48) {
                CHECK(result < 0 && outcome == REIST_FILE_OBJECT_UNKNOWN);
                CHECK(file_object_guard_can_open(&guard, 1, now) == -REIST_EIO);
                CHECK(!file_object_guard_cleanup(&guard, owner));
                CHECK(file_object_guard_can_open(&guard, 1, now) == -REIST_EIO);
            } else CHECK(!result && outcome == REIST_FILE_OBJECT_DURABLE_COMMIT);
            /* Power-cycle only for the disk recovery campaign. This is NOT
             * a service restart clearing a fence in the same running kernel. */
            memcpy(disk, stable, sizeof(disk));
            reset_guard();
            CHECK(!begin());
            CHECK(!reist_fat32_transaction_finish(&transaction, false, &outcome));
            bool old = disk[64][0] == 0x11;
            for (unsigned i = 0; i < 20; ++i) {
                memset(payload, (old ? 0x11 : 0x70)+i, 512);
                CHECK(!memcmp(payload, disk[64+i], 512));
            }
            CHECK(!memcmp(disk[8], disk[31], 512));
            CHECK(((ata_journal_record_t*)disk[8])->state == ATA_JOURNAL_CLEAN);
            /* No unrelated sector changes. Undo/header sectors are evidence. */
            for (unsigned i = 0; i < 512; ++i)
                if (!(i >= 8 && i < 29) && i != 31 && !(i >= 64 && i < 84))
                    CHECK(!memcmp(disk[i], original[i], 512));
        }
    }
}

int main(void) {
    request_validation();
    guard_authority();
    transactions();
    interruption_campaign();
    printf("JOURNAL_HANDOFF checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
#endif
