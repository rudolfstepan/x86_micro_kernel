/* Bounded Ring-3 receive diagnostic for the local acceptance peer, protocol v1. */
#include "x86os.h"
#include <stdint.h>

#define TEST_BYTES (1024U * 1024U)
static uint8_t buffer[X86OS_TCP_RECEIVE_CAPACITY];
static int equal(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int check_bytes(uint32_t offset, uint32_t count) {
    if (count > sizeof(buffer) || count > TEST_BYTES - offset) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t at = offset + i;
        if (buffer[i] != (uint8_t)(at * 29U + at / 251U)) return -1;
    }
    return 0;
}
static int file_check(void) {
    int fd = x86os_open("/tmp/nettest.bin");
    if (fd < 0) return fd;
    int rc = 0; uint32_t offset = 0;
    for (uint32_t step = 0; step <= TEST_BYTES && rc == 0; ++step) {
        int n = x86os_read(fd, buffer, sizeof(buffer));
        if (n == 0) { rc = offset == TEST_BYTES ? 0 : -84; break; }
        if (n < 0 || check_bytes(offset, (uint32_t)n) != 0) { rc = -84; break; }
        offset += (uint32_t)n;
    }
    if (x86os_close(fd) != 0) rc = -5;
    if (rc == 0) x86os_puts("NETTEST_FILE_OK bytes=1048576\n");
    return rc;
}
static int child_state(int pid, uint32_t generation) {
    x86os_process_identity_t identity;
    int rc = x86os_process_identity_of(pid, &identity);
    if (rc != 0 && rc != -3) return -5;
    if (rc == 0 && (identity.pid != pid || identity.generation != generation)) return -5;
    for (uint32_t i = 0; i < 32; ++i) {
        x86os_process_info_t info;
        int n = x86os_process_info(i, &info);
        if (n <= 0) break;
        if (info.pid == pid) {
            if (info.parent_pid != x86os_getpid() || (rc == -3 && info.state != X86OS_PROCESS_ZOMBIE)) return -5;
            return info.state;
        }
    }
    return -5;
}
static int cancel_check(void) {
    const char *args[] = {"nettest", "blocked"};
    int pid = x86os_spawnv("/sbin/nettest.prg", 2, args);
    if (pid <= 0) return -5;
    x86os_process_identity_t child;
    if (x86os_process_identity_of(pid, &child) != 0 || child.pid != pid || child.generation == 0) return -5;
    uint32_t generation = child.generation;
    /* The guest evidence additionally requires the child's actual connected
     * marker before this kill. No claim based on sleeping alone. */
    (void)x86os_sleep_ms(1000);
    int state = child_state(pid, generation);
    if (state < 0) return -5;
    if (state != X86OS_PROCESS_ZOMBIE && x86os_kill(pid) != 0) return -5;
    uint64_t start = 0, now = 0;
    if (x86os_monotonic_ms(&start) != 0) return -5;
    for (uint32_t step = 0; step < 1000; ++step) {
        state = child_state(pid, generation);
        if (state < 0) return -5;
        if (state == X86OS_PROCESS_ZOMBIE) {
            int status = -1;
            if (x86os_wait(pid, &status) != pid || status != 143) return -5;
            x86os_puts("NETTEST_CANCEL_REAP_OK status=143\n"); return 0;
        }
        if (x86os_monotonic_ms(&now) != 0 || now < start || now - start >= 1000) break;
        if (x86os_sleep_ms(1) != 0) break;
    }
    return -110;
}
int main(int argc, char **argv) {
    if (argc != 2) { x86os_puts("usage: nettest stream|slow|timeout|reset|cancel|file\n"); return 2; }
    if (equal(argv[1], "file")) {
        int rc = file_check();
        if (rc != 0) x86os_puts("NETTEST_FAIL file\n");
        return rc != 0;
    }
    if (equal(argv[1], "cancel")) {
        int rc = cancel_check();
        if (rc != 0) x86os_puts("NETTEST_FAIL cancel owner\n");
        return rc != 0;
    }
    char mode = equal(argv[1], "stream") ? 's' : equal(argv[1], "slow") ? 'w' :
        equal(argv[1], "timeout") ? 't' : equal(argv[1], "reset") ? 'r' :
        equal(argv[1], "blocked") ? 'c' : 0;
    if (mode == 0) return 2;
    /* All four slots must be reusable, including after killed owners. */
    x86os_tcp_socket_t slots[4] = {0}; int rc = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        rc = x86os_tcp_socket_open(&slots[i]);
        if (rc != 0) goto cleanup;
    }
    for (uint32_t i = 1; i < 4; ++i) {
        rc = x86os_tcp_socket_close(slots[i], 0); slots[i] = 0;
        if (rc != 0) goto cleanup;
    }
    x86os_tcp_connect_t connect = {
        .version = X86OS_TCP_SOCKET_VERSION, .struct_size = sizeof(connect),
        .socket = slots[0], .destination_ip = 0x0a000202U,
        .destination_port = 18766, .timeout_ms = 3000,
    };
    rc = x86os_tcp_connect(&connect); if (rc != 0) goto cleanup;
    x86os_tcp_io_t io = {
        .version = X86OS_TCP_SOCKET_VERSION, .struct_size = sizeof(io),
        .socket = slots[0], .length = 1, .timeout_ms = 1000,
    };
    rc = x86os_tcp_send(&io, &mode); if (rc != 1) { rc = -5; goto cleanup; }
    uint64_t start = 0, now = 0;
    if (x86os_monotonic_ms(&start) != 0) { rc = -5; goto cleanup; }
    if (mode == 'w' && x86os_sleep_ms(250) != 0) { rc = -5; goto cleanup; }
    if (mode == 'c') x86os_puts("NETTEST_CANCEL_READY\n");
    uint32_t offset = 0; rc = -110;
    for (uint32_t step = 0; step <= TEST_BYTES; ++step) {
        if (x86os_monotonic_ms(&now) != 0 || now < start || now - start >= 5000) break;
        io.length = sizeof(buffer); io.timeout_ms = mode == 't' ? 250 : (uint32_t)(5000 - (now - start));
        int n = x86os_tcp_receive(&io, buffer);
        if (mode == 't') { rc = n == -110 ? 0 : -84; break; }
        if (mode == 'r') { rc = n == 0 ? 0 : -84; break; } /* current ABI: reset closes stream */
        if (mode == 'c') { rc = -84; break; } /* must be killed, never finish normally */
        if (n == 0) { rc = offset == TEST_BYTES ? 0 : -84; break; }
        if (n < 0 || (uint32_t)n != io.length || check_bytes(offset, (uint32_t)n) != 0) { rc = -84; break; }
        offset += (uint32_t)n;
    }
    if (x86os_monotonic_ms(&now) != 0 || now < start || now - start > 5000) rc = -110;
    if (rc == 0) {
        x86os_puts("NETTEST_OK mode="); x86os_putchar(mode);
        x86os_puts(" bytes="); x86os_print_number((int)offset);
        x86os_puts(" elapsed_ms="); x86os_print_number((int)(now - start)); x86os_putchar('\n');
    } else {
        x86os_puts("NETTEST_RX bytes="); x86os_print_number((int)offset); x86os_putchar('\n');
    }
cleanup:
    for (uint32_t i = 0; i < 4; ++i)
        if (slots[i] && x86os_tcp_socket_close(slots[i], 0) != 0) rc = -5;
    if (rc != 0) { x86os_puts("NETTEST_FAIL rc="); x86os_print_number(rc); x86os_putchar('\n'); }
    return rc != 0;
}
