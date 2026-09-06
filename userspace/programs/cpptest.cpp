/* C++ profile 1 proof, launched through the ordinary Ring-3 shell. */
#include <new>
#include <stdint.h>
#include <string.h>
#include <reist/libc.h>
#include <reist/cpp.h>
#include <x86os.h>

namespace {
constexpr unsigned mib = 1024U * 1024U;
unsigned constructed, destroyed;
const char *phase = "startup";
struct Object {
    unsigned value;
    Object() noexcept : value(42) { ++constructed; }
    ~Object() noexcept { ++destroyed; }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&& other) noexcept : value(other.value) {
        other.value = 0; ++constructed;
    }
};
struct alignas(4096) Aligned : Object {};
static_assert(sizeof(reist_libc_stats_t) == 24);
static_assert(sizeof(x86os_process_identity_t) == 16);
static_assert(__builtin_offsetof(reist_libc_stats_t, live_bytes) == 16);

bool lifetime() {
    if (constructed || destroyed || ::operator new(4, std::nothrow)) return false;
    if (reist_libc_init_process(8 * mib)) return false;
    x86os_memory_stats_t before{}, after{};
    if (x86os_memory_stats(&before)) return false;
    {
        Object first;
        Object moved(static_cast<Object&&>(first));
        if (first.value || moved.value != 42) return false;
    }
    alignas(Object) unsigned char storage[sizeof(Object)];
    Object *placed = new(storage) Object;
    placed->~Object();
    Object *single = new(std::nothrow) Object;
    Object *array = new(std::nothrow) Object[7];
    Aligned *aligned = new(std::nothrow) Aligned[2];
    if (!single || !array || !aligned || array[6].value != 42 || aligned[1].value != 42 ||
        reinterpret_cast<uintptr_t>(aligned) % 4096) return false;
    volatile size_t huge = SIZE_MAX / sizeof(Object) + 1;
    if (new(std::nothrow) Object[huge] || ::operator new(SIZE_MAX, std::nothrow) ||
        ::operator new(SIZE_MAX, std::align_val_t(4096), std::nothrow) ||
        !reist_libc_reset() || single->value != 42 || array[6].value != 42) return false;
    delete single; delete[] array; delete[] aligned;
    void *zero = ::operator new(0, std::nothrow);
    if (!zero) return false;
    ::operator delete(zero);
    ::operator delete(nullptr);
    ::operator delete[](nullptr, std::align_val_t(4096));
    reist_libc_stats_t stats{REIST_LIBC_VERSION, sizeof(stats), 0, 0, 0, 0};
    if (constructed != destroyed || reist_libc_stats(&stats) || stats.capacity ||
        stats.live_objects || x86os_memory_stats(&after) ||
        before.allocated_frame_bytes != after.allocated_frame_bytes) return false;
    x86os_puts("REIST_CPP_LIFETIME_OK\nREIST_CPP_BACKING_RETURN_OK\n");
    return true;
}

void decimal(char out[11], uint32_t value) {
    char reverse[10]; unsigned count = 0;
    do { reverse[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && count < 10);
    for (unsigned i = 0; i < count; ++i) out[i] = reverse[count - i - 1];
    out[count] = 0;
}
uint32_t parse(const char *s) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 10; ++i) {
        if (!s[i]) return value;
        if (s[i] < '0' || s[i] > '9' || value > (UINT32_MAX - unsigned(s[i] - '0')) / 10) return 0;
        value = value * 10 + unsigned(s[i] - '0');
    }
    return s[10] ? 0 : value;
}
int send(x86os_ipc_handle_t endpoint) {
    x86os_ipc_message_t msg{X86OS_IPC_MESSAGE_VERSION, sizeof(msg), 1, {0x43}};
    return x86os_ipc_send_timeout(endpoint, &msg, 1000);
}
int receive(x86os_ipc_handle_t endpoint, uint32_t timeout) {
    x86os_ipc_message_t msg{X86OS_IPC_MESSAGE_VERSION, sizeof(msg), 0, {0}};
    int result = x86os_ipc_receive_timeout(endpoint, &msg, timeout);
    return result ? result : (msg.version != X86OS_IPC_MESSAGE_VERSION ||
        msg.struct_size != sizeof(msg) || msg.length != 1 || msg.payload[0] != 0x43 ? -22 : 0);
}
int wait_child(int pid, uint32_t generation, int expected) {
    uint64_t start = 0, now = 0;
    if (x86os_monotonic_ms(&start)) return -1;
    for (unsigned round = 0; round < 10000; ++round) {
        x86os_process_identity_t identity{};
        int identity_result = x86os_process_identity_of(pid, &identity);
        /* PROCESS_IDENTITY deliberately grants no live authority for a zombie.
         * Its unreaped PID cannot be reused; wait below additionally verifies
         * parent ownership. A different live generation is still rejected. */
        if ((identity_result && identity_result != -3) ||
            (!identity_result && identity.generation != generation)) return -1;
        for (unsigned i = 0; i < 32; ++i) {
            x86os_process_info_t info{};
            int result = x86os_process_info(i, &info);
            if (result < 0) return -1;
            if (!result) break;
            if (info.pid == pid && info.state == X86OS_PROCESS_ZOMBIE) {
                int status = -1;
                return x86os_wait(pid, &status) == pid && status == expected && x86os_wait(pid, &status) < 0 ? 0 : -1;
            }
        }
        if (x86os_monotonic_ms(&now) || now < start || now - start >= 10000 || x86os_sleep_ms(1)) break;
    }
    return -1;
}
int child(const char *mode, x86os_ipc_handle_t command, x86os_ipc_handle_t reply) {
    /* Delegation happens after spawn: receive only after authority arrives. */
    uint64_t start = 0, now = 0;
    if (x86os_monotonic_ms(&start)) return 1;
    bool ready = false;
    for (unsigned i = 0; i < 5000; ++i) {
        int result = receive(command, 0);
        if (!result) { ready = true; break; }
        if (result != -11 && result != -13 && result != -110) return 1;
        if (x86os_monotonic_ms(&now) || now < start || now - start >= 5000 || x86os_sleep_ms(1)) break;
    }
    if (!ready || reist_libc_init_process(8 * mib)) return 1;
    unsigned char *p = new(std::nothrow) unsigned char[mib];
    if (!p) return 1;
    memset(p, 0xc3, mib);
    if (send(reply) || receive(command, 30000)) return 1;
    if (!strcmp(mode, "--oom")) {
        /* Do not optimize away the failed allocation expression. */
        void *failure = ::operator new(SIZE_MAX);
        (void)failure; return 2;
    }
    if (!strcmp(mode, "--fault")) { __asm__ volatile("ud2"); return 2; }
    if (strcmp(mode, "--normal")) return 2;
    if (p[0] != 0xc3 || p[mib - 1] != 0xc3) return 1;
    delete[] p;
    return reist_libc_reset() ? 1 : 0;
}
bool containment() {
    unsigned char *canary = new(std::nothrow) unsigned char[mib];
    if (!canary) return false;
    memset(canary, 0x5a, mib);
    const char *modes[]{"--oom", "--fault", "--hold", "--normal"};
    const int expected[]{REIST_CPP_ALLOCATION_FAILURE, 134, 143, 0};
    int previous_pid = 0;
    uint32_t previous_generation = 0;
    for (unsigned round = 0; round < 4; ++round) {
        phase = modes[round];
        x86os_ipc_handle_t command = 0, reply = 0;
        int pid = 0;
        x86os_process_identity_t identity{};
        x86os_memory_stats_t before{}, after{};
        bool ok = false;
        if (x86os_ipc_create(&command) || x86os_ipc_create(&reply)) {
            if (command) x86os_ipc_close(command);
            delete[] canary; return false;
        }
        do {
            if (x86os_memory_stats(&before)) break;
            char a[11], b[11]; decimal(a, command); decimal(b, reply);
            const char *args[]{"/usr/bin/cpptest.prg", modes[round], a, b};
            pid = x86os_spawnv(args[0], 4, args);
            if (pid <= 0 || x86os_process_identity_of(pid, &identity) || !identity.generation ||
                (pid == previous_pid && identity.generation == previous_generation)) break;
            previous_pid = pid; previous_generation = identity.generation;
            if (x86os_ipc_delegate(command, pid, X86OS_IPC_RIGHT_RECEIVE) ||
                x86os_ipc_delegate(reply, pid, X86OS_IPC_RIGHT_SEND) ||
                send(command) || receive(reply, 5000)) break;
            if (round == 2 ? x86os_kill(pid) : send(command)) break;
            if (wait_child(pid, identity.generation, expected[round])) break;
            pid = 0;
            bool intact = true;
            for (unsigned i = 0; i < mib; ++i) if (canary[i] != 0x5a) { intact = false; break; }
            if (!intact) break;
            if (x86os_memory_stats(&after) || before.allocated_frame_bytes != after.allocated_frame_bytes) break;
            ok = true;
        } while (false);
        if (pid > 0) {
            x86os_process_identity_t current{};
            if (!x86os_process_identity_of(pid, &current) && current.generation == identity.generation) {
                x86os_kill(pid); wait_child(pid, identity.generation, 143);
            }
        }
        int close_command = x86os_ipc_close(command), close_reply = x86os_ipc_close(reply);
        if (!ok || close_command || close_reply) { delete[] canary; return false; }
        x86os_puts("REIST_CPP_REAP_OK mode="); x86os_puts(modes[round]); x86os_puts("\n");
    }
    delete[] canary;
    return !reist_libc_reset();
}
}

extern "C" int main(int argc, char **argv) {
    if (argc == 4) return child(argv[1], parse(argv[2]), parse(argv[3]));
    if (argc != 1) return 2;
    if (!lifetime() || !containment()) {
        x86os_puts("REIST_CPP_RUNTIME_FAIL phase="); x86os_puts(phase); x86os_puts("\n");
        return 1;
    }
    x86os_puts("REIST_CPP_RUNTIME_OK\n");
    return 0;
}
