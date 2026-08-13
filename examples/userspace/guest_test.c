#include "x86os.h"

#define WAIT_STRESS_ITERATIONS 64

static int bytes_equal(const char *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

static int wait_for_expected(const char *path, int expected_status) {
    int pid = x86os_spawn(path);
    if (pid <= 0) return -1;

    int status = -1;
    if (x86os_wait(pid, &status) != pid || status != expected_status) return -1;

    /* A zombie may be collected exactly once. */
    if (x86os_wait(pid, &status) >= 0) return -1;
    return 0;
}

static int test_wait_wakeup(void) {
    if (wait_for_expected("CHILDEX.PRG", 37) != 0) return -1;
    x86os_memory_stats_t before;
    x86os_memory_stats_t after;
    if (x86os_memory_stats(&before) != 0) return -1;
    for (int iteration = 0; iteration < WAIT_STRESS_ITERATIONS; ++iteration) {
        if (wait_for_expected("CHILDEX.PRG", 37) != 0) return -1;
    }
    if (x86os_memory_stats(&after) != 0) return -1;
    return after.allocated_frame_bytes == before.allocated_frame_bytes &&
           after.heap_used_bytes == before.heap_used_bytes
        ? 0 : -1;
}

static int test_file_io(void) {
    static const char path[] = "GUEST.TMP";
    static char expected[1537];
    static char actual[1537];

    for (size_t index = 0; index < sizeof(expected); ++index) {
        expected[index] = (char)('A' + index % 23U);
    }

    (void)x86os_unlink(path);
    int descriptor = x86os_create(path);
    if (descriptor < 0) return -1;
    if (x86os_write(descriptor, expected, sizeof(expected)) !=
        (int)sizeof(expected)) {
        (void)x86os_close(descriptor);
        (void)x86os_unlink(path);
        return -1;
    }
    int initial_sync = x86os_fsync(descriptor);
    int initial_close = x86os_close(descriptor);
    if (initial_sync != 0 || initial_close != 0) {
        (void)x86os_unlink(path);
        return -1;
    }

    descriptor = x86os_open(path);
    if (descriptor < 0) {
        (void)x86os_unlink(path);
        return -1;
    }
    int amount = x86os_read(descriptor, actual, sizeof(actual));
    char extra;
    int eof = x86os_read(descriptor, &extra, 1);
    int close_result = x86os_close(descriptor);
    x86os_file_info_t info;
    int stat_result = x86os_stat(path, &info);
    if (amount != (int)sizeof(actual) || eof != 0 || close_result != 0 ||
        stat_result != 0 || info.type != X86OS_FILE ||
        info.size != sizeof(expected) ||
        !bytes_equal(actual, expected, sizeof(actual))) {
        (void)x86os_unlink(path);
        return -1;
    }

    static const char replacement_path[] = "GSTNEW.TMP";
    static const char replacement[] = "REIST atomic rename replacement";
    (void)x86os_unlink(replacement_path);
    descriptor = x86os_create(replacement_path);
    if (descriptor < 0) {
        (void)x86os_unlink(path);
        return -1;
    }
    int replacement_write = x86os_write(
        descriptor, replacement, sizeof(replacement) - 1U);
    int replacement_sync = x86os_fsync(descriptor);
    int replacement_close = x86os_close(descriptor);
    if (replacement_write != (int)(sizeof(replacement) - 1U) ||
        replacement_sync != 0 || replacement_close != 0 ||
        x86os_rename(replacement_path, path) != 0 ||
        x86os_stat(replacement_path, &info) == 0) {
        (void)x86os_unlink(replacement_path);
        (void)x86os_unlink(path);
        return -1;
    }
    descriptor = x86os_open(path);
    if (descriptor < 0) return -1;
    char renamed[sizeof(replacement) - 1U];
    amount = x86os_read(descriptor, renamed, sizeof(renamed));
    close_result = x86os_close(descriptor);
    return amount == (int)sizeof(renamed) && close_result == 0 &&
           bytes_equal(renamed, replacement, sizeof(renamed)) &&
           x86os_unlink(path) == 0 ? 0 : -1;
}

static int process_state_for_pid(int pid) {
    for (uint32_t index = 0; index < 32U; ++index) {
        x86os_process_info_t info;
        int result = x86os_process_info(index, &info);
        if (result <= 0) break;
        if (info.pid == pid) return info.state;
    }
    return -1;
}

static int process_info_for_pid(int pid, x86os_process_info_t *result) {
    if (result == NULL) return -1;
    for (uint32_t index = 0; index < 32U; ++index) {
        x86os_process_info_t info;
        int status = x86os_process_info(index, &info);
        if (status <= 0) break;
        if (info.pid == pid) {
            *result = info;
            return 0;
        }
    }
    return -1;
}

static int test_scheduler_time(void) {
    uint64_t start;
    uint64_t now;
    if (x86os_monotonic_ms(&start) != 0 ||
        x86os_monotonic_ms((uint64_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_sleep_ms(0) != 0) {
        return -1;
    }
    for (unsigned int iteration = 0; iteration < 1000U; ++iteration) {
        uint64_t sampled;
        if (x86os_monotonic_ms(&sampled) != 0 || sampled < start) return -1;
        start = sampled;
    }

    /* A direct yield must hand execution to the already-ready child. */
    int quick_pid = x86os_spawn("CHILDEX.PRG");
    if (quick_pid <= 0 || x86os_yield() != 0 ||
        process_state_for_pid(quick_pid) != X86OS_PROCESS_ZOMBIE) {
        return -1;
    }
    int status = -1;
    if (x86os_wait(quick_pid, &status) != quick_pid || status != 37) return -1;

    uint64_t short_start;
    if (x86os_monotonic_ms(&short_start) != 0 ||
        x86os_sleep_ms(30) != 0 || x86os_monotonic_ms(&now) != 0 ||
        now - short_start < 30U || now - short_start > 2000U) {
        return -1;
    }

    int sleeper_pid = x86os_spawn("SLEEPER.PRG");
    if (sleeper_pid <= 0 || x86os_yield() != 0 ||
        process_state_for_pid(sleeper_pid) != X86OS_PROCESS_SLEEPING) {
        return -1;
    }

    /* Other work must complete while the sleeper is absent from the runnable
     * set.  Waiting for the sleeper then proves its deadline wakeup. */
    if (wait_for_expected("CHILDEX.PRG", 37) != 0 ||
        x86os_wait(sleeper_pid, &status) != sleeper_pid || status != 41 ||
        x86os_monotonic_ms(&now) != 0 || now - start < 400U) {
        return -1;
    }

    start = now;
    x86os_delay(25);
    if (x86os_monotonic_ms(&now) != 0 || now - start < 25U) return -1;

    /* Killing a sleeper must unlink its intrusive queue node before its task
     * slot is reused by the next child. */
    sleeper_pid = x86os_spawn("SLEEPER.PRG");
    if (sleeper_pid <= 0 || x86os_yield() != 0 ||
        process_state_for_pid(sleeper_pid) != X86OS_PROCESS_SLEEPING ||
        x86os_kill(sleeper_pid) != 0 ||
        x86os_wait(sleeper_pid, &status) != sleeper_pid || status != 143 ||
        wait_for_expected("CHILDEX.PRG", 37) != 0) {
        return -1;
    }
    return 0;
}

static int test_memory_accounting(void) {
    x86os_memory_stats_t before;
    x86os_memory_stats_t allocated;
    x86os_memory_stats_t reclaimed;
    if (x86os_memory_stats(&before) != 0 ||
        x86os_memory_stats((x86os_memory_stats_t*)(uintptr_t)0x1000U) != -14 ||
        before.version != X86OS_MEMORY_STATS_VERSION ||
        before.struct_size != sizeof(before) ||
        before.detected_usable_bytes < before.managed_bytes ||
        before.managed_bytes != before.reserved_bytes +
            before.allocated_frame_bytes + before.free_frame_bytes ||
        before.heap_arena_count < 2U ||
        before.heap_used_bytes + before.heap_free_bytes >
            before.heap_capacity_bytes ||
        before.heap_largest_free_block > before.heap_free_bytes) {
        return -1;
    }

    const size_t allocation_size = 3U * 4096U + 17U;
    void *allocation = x86os_malloc(allocation_size);
    if (allocation == NULL || x86os_memory_stats(&allocated) != 0 ||
        allocated.allocated_frame_bytes <= before.allocated_frame_bytes ||
        allocated.free_frame_bytes >= before.free_frame_bytes) {
        x86os_free(allocation);
        return -1;
    }
    ((volatile uint8_t*)allocation)[0] = 0xA5U;
    ((volatile uint8_t*)allocation)[allocation_size - 1U] = 0x5AU;
    if (((volatile uint8_t*)allocation)[0] != 0xA5U ||
        ((volatile uint8_t*)allocation)[allocation_size - 1U] != 0x5AU) {
        x86os_free(allocation);
        return -1;
    }

    x86os_free(allocation);
    if (x86os_memory_stats(&reclaimed) != 0 ||
        reclaimed.free_frame_bytes < allocated.free_frame_bytes + 4U * 4096U ||
        reclaimed.managed_bytes != reclaimed.reserved_bytes +
            reclaimed.allocated_frame_bytes + reclaimed.free_frame_bytes) {
        return -1;
    }
    return 0;
}

static int test_task_capacity_and_parenting(void) {
    /* The reserved REIST supervisor worker, shell and GTEST occupy three of
     * the eight task slots. Safety supervision is capacity, not best effort. */
    int children[5];
    int parent_pid = x86os_getpid();
    for (size_t index = 0; index < sizeof(children) / sizeof(children[0]);
         ++index) {
        children[index] = x86os_spawn("SLEEPER.PRG");
        if (children[index] <= 0) return -1;
    }
    if (x86os_spawn("SLEEPER.PRG") >= 0) return -1;

    for (size_t index = 0; index < sizeof(children) / sizeof(children[0]);
         ++index) {
        x86os_process_info_t info;
        if (process_info_for_pid(children[index], &info) != 0 ||
            info.parent_pid != parent_pid ||
            (info.state != X86OS_PROCESS_READY &&
             info.state != X86OS_PROCESS_RUNNING &&
             info.state != X86OS_PROCESS_SLEEPING)) {
            return -1;
        }
    }

    for (size_t index = 0; index < sizeof(children) / sizeof(children[0]);
         ++index) {
        int status = -1;
        if (x86os_kill(children[index]) != 0 ||
            x86os_wait(children[index], &status) != children[index] ||
            status != 143) return -1;
    }
    return wait_for_expected("CHILDEX.PRG", 37);
}

int main(void) {
    x86os_puts("GUEST_TEST_BEGIN\n");

    if (test_wait_wakeup() != 0) {
        x86os_puts("TEST_FAIL WAIT\n");
        return 1;
    }
    x86os_puts("TEST_STAGE WAIT_OK\n");

    if (test_file_io() != 0) {
        x86os_puts("TEST_FAIL FILE_IO\n");
        return 2;
    }
    x86os_puts("TEST_STAGE FILE_IO_OK\n");

    if (test_scheduler_time() != 0) {
        x86os_puts("TEST_FAIL SCHED_TIME\n");
        return 3;
    }
    x86os_puts("TEST_STAGE SCHED_TIME_OK\n");

    if (test_memory_accounting() != 0) {
        x86os_puts("TEST_FAIL MEMORY\n");
        return 4;
    }
    x86os_puts("TEST_STAGE MEMORY_OK\n");

    if (test_task_capacity_and_parenting() != 0) {
        x86os_puts("TEST_FAIL TASK_CAPACITY\n");
        return 5;
    }
    x86os_puts("TEST_STAGE TASK_CAPACITY_OK\n");

    if (wait_for_expected("FAULTDE.PRG", 128) != 0 ||
        wait_for_expected("FAULTUD.PRG", 134) != 0 ||
        wait_for_expected("FAULTPF.PRG", 142) != 0 ||
        wait_for_expected("FAULTSTK.PRG", 142) != 0) {
        x86os_puts("TEST_FAIL EXCEPTIONS\n");
        return 6;
    }
    x86os_puts("TEST_STAGE EXCEPTIONS_OK\n");
    x86os_puts("TEST_OK\n");
    return 0;
}
