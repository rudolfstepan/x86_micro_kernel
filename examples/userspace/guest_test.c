#include "x86os.h"

#define WAIT_STRESS_ITERATIONS 64

static int text_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return 0;
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int parse_ipc_handle(const char *text, x86os_ipc_handle_t *handle) {
    if (text == NULL || handle == NULL || *text == '\0') return -1;
    uint32_t value = 0U;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return -1;
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (UINT32_MAX - digit) / 10U) return -1;
        value = value * 10U + digit;
        ++text;
    }
    if (value == X86OS_IPC_INVALID_HANDLE) return -1;
    *handle = value;
    return 0;
}

static void format_ipc_handle(x86os_ipc_handle_t handle, char text[11]) {
    char reverse[10];
    size_t length = 0U;
    do {
        reverse[length++] = (char)('0' + handle % 10U);
        handle /= 10U;
    } while (handle != 0U);
    for (size_t index = 0; index < length; ++index) {
        text[index] = reverse[length - index - 1U];
    }
    text[length] = '\0';
}

static void ipc_message_prepare(x86os_ipc_message_t *message) {
    message->version = X86OS_IPC_MESSAGE_VERSION;
    message->struct_size = sizeof(*message);
    message->length = 0U;
}

static void ipc_message_set(x86os_ipc_message_t *message, const char *text) {
    ipc_message_prepare(message);
    while (text[message->length] != '\0' &&
           message->length < X86OS_IPC_MAX_MESSAGE_SIZE) {
        message->payload[message->length] = (uint8_t)text[message->length];
        ++message->length;
    }
}

static int ipc_message_is(const x86os_ipc_message_t *message,
                          const char *text) {
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    if (message->version != X86OS_IPC_MESSAGE_VERSION ||
        message->struct_size != sizeof(*message) ||
        message->length != length) return 0;
    for (size_t index = 0; index < length; ++index) {
        if (message->payload[index] != (uint8_t)text[index]) return 0;
    }
    return 1;
}

#define SERVICE_PROTOCOL_HEADER_SIZE 8U

static int service_request_set(x86os_ipc_message_t *message,
                               uint32_t request_id, const char *text) {
    if (request_id == 0U) return -1;
    ipc_message_prepare(message);
    message->payload[0] = 'R';
    message->payload[1] = 'Q';
    message->payload[2] = '1';
    message->payload[3] = 0U;
    message->payload[4] = (uint8_t)request_id;
    message->payload[5] = (uint8_t)(request_id >> 8U);
    message->payload[6] = (uint8_t)(request_id >> 16U);
    message->payload[7] = (uint8_t)(request_id >> 24U);
    message->length = SERVICE_PROTOCOL_HEADER_SIZE;
    while (*text != '\0' && message->length < X86OS_IPC_MAX_MESSAGE_SIZE)
        message->payload[message->length++] = (uint8_t)*text++;
    return *text == '\0' ? 0 : -1;
}

static int service_response_is(const x86os_ipc_message_t *message,
                               uint32_t request_id, const char *text) {
    if (request_id == 0U || message->length < SERVICE_PROTOCOL_HEADER_SIZE ||
        message->payload[0] != 'R' || message->payload[1] != 'S' ||
        message->payload[2] != '1' || message->payload[3] != 0U)
        return 0;
    uint32_t received = (uint32_t)message->payload[4] |
        ((uint32_t)message->payload[5] << 8U) |
        ((uint32_t)message->payload[6] << 16U) |
        ((uint32_t)message->payload[7] << 24U);
    if (received != request_id) return 0;
    size_t length = 0U;
    while (text[length] != '\0') ++length;
    if (message->length != SERVICE_PROTOCOL_HEADER_SIZE + length) return 0;
    for (size_t index = 0U; index < length; ++index) {
        if (message->payload[SERVICE_PROTOCOL_HEADER_SIZE + index] !=
            (uint8_t)text[index]) return 0;
    }
    return 1;
}

static void ipc_message_set_test_arp_frame(x86os_ipc_message_t *message) {
    ipc_message_prepare(message);
    message->payload[0] = 'N';
    message->payload[1] = 'E';
    message->payload[2] = 'T';
    message->payload[3] = '1';
    for (size_t index = 4U; index < 16U; ++index)
        message->payload[index] = (uint8_t)index;
    message->payload[16] = 0x08U;
    message->payload[17] = 0x06U;
    message->payload[18] = 0U;
    message->payload[19] = 1U;
    message->payload[20] = 0x08U;
    message->payload[21] = 0U;
    message->payload[22] = 6U;
    message->payload[23] = 4U;
    message->payload[24] = 0U;
    message->payload[25] = 2U;
    for (size_t index = 26U; index < 46U; ++index)
        message->payload[index] = (uint8_t)index;
    message->length = 46U;
}

static void ipc_message_set_test_arp_identity(x86os_ipc_message_t *message) {
    ipc_message_set_test_arp_frame(message);
    message->payload[3] = 'X';
    for (size_t index = 0U; index < 6U; ++index) {
        message->payload[4U + index] = (uint8_t)(0xA0U + index);
        message->payload[10U + index] = (uint8_t)(0xB0U + index);
        message->payload[26U + index] = message->payload[10U + index];
        message->payload[36U + index] = message->payload[4U + index];
        message->payload[54U + index] = message->payload[4U + index];
    }
    for (size_t index = 0U; index < 4U; ++index) {
        message->payload[32U + index] = (uint8_t)(10U + index);
        message->payload[42U + index] = (uint8_t)(20U + index);
        message->payload[46U + index] = message->payload[32U + index];
        message->payload[50U + index] = message->payload[42U + index];
    }
    message->length = 60U;
}

static int ipc_child_main(const char *mode, const char *handle_text) {
    x86os_ipc_handle_t handle;
    if (parse_ipc_handle(handle_text, &handle) != 0) return 70;
    /* Delegation is explicit and occurs immediately after spawn.  Yield the
     * fresh child through a bounded sleep so the parent can publish its
     * generation-scoped capability before the first IPC operation. */
    if (x86os_sleep_ms(10U) != 0) return 76;

    if (text_equal(mode, "IPC_ECHO")) {
        /* Explicit delegation grants SEND/RECEIVE, never CONTROL. */
        if (x86os_ipc_close(handle) >= 0) return 71;
        x86os_ipc_message_t message;
        ipc_message_prepare(&message);
        if (x86os_ipc_receive(handle, &message) != 0 ||
            !ipc_message_is(&message, "PING")) return 72;
        ipc_message_set(&message, "PONG");
        if (x86os_ipc_send(handle, &message) != 0) return 73;
        (void)x86os_yield();
        return 53;
    }

    if (text_equal(mode, "IPC_WAIT_CLOSE")) {
        x86os_ipc_message_t message;
        ipc_message_prepare(&message);
        return x86os_ipc_receive(handle, &message) < 0 ? 55 : 74;
    }

    if (text_equal(mode, "IPC_EXIT")) return 54;
    return 75;
}

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

static int wait_for_process_state(int pid, int expected, uint32_t timeout_ms) {
    uint64_t start = 0U;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&start) != 0) return -1;
    do {
        if (process_state_for_pid(pid) == expected) return 0;
        if (x86os_yield() != 0 || x86os_monotonic_ms(&now) != 0) return -1;
    } while (now - start < timeout_ms);
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

static int spawn_ipc_child(const char *mode, x86os_ipc_handle_t handle) {
    char handle_text[11];
    format_ipc_handle(handle, handle_text);
    const char *arguments[] = {"GTEST.PRG", mode, handle_text};
    int pid = x86os_spawnv("GTEST.PRG", 3, arguments);
    if (pid <= 0) return pid;
    if (x86os_ipc_delegate(handle, pid,
            X86OS_IPC_RIGHT_SEND | X86OS_IPC_RIGHT_RECEIVE) != 0) {
        (void)x86os_kill(pid);
        int status;
        (void)x86os_wait(pid, &status);
        return -1;
    }
    return pid;
}

static int wait_for_ipc_child(int pid, int expected_status) {
    int status = -1;
    return pid > 0 && x86os_wait(pid, &status) == pid &&
           status == expected_status ? 0 : -1;
}

static int test_ipc_capabilities(void) {
    x86os_memory_stats_t before;
    x86os_memory_stats_t after;
    if (x86os_memory_stats(&before) != 0 ||
        x86os_ipc_create(
            (x86os_ipc_handle_t*)(uintptr_t)0x1000U) != -14) return -1;

    x86os_ipc_handle_t handle = X86OS_IPC_INVALID_HANDLE;
    if (x86os_ipc_create(&handle) != 0 ||
        handle == X86OS_IPC_INVALID_HANDLE) return -1;
    if (x86os_ipc_delegate(handle, x86os_getpid(),
            X86OS_IPC_RIGHT_SEND) >= 0 ||
        x86os_ipc_delegate(handle, 0, X86OS_IPC_RIGHT_SEND) >= 0 ||
        x86os_ipc_delegate(handle, x86os_getpid() + 10000,
            X86OS_IPC_RIGHT_SEND) >= 0 ||
        x86os_ipc_delegate(handle, x86os_getpid(),
            X86OS_IPC_RIGHT_CONTROL) >= 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }
    int child = spawn_ipc_child("IPC_ECHO", handle);
    if (child <= 0 ||
        wait_for_process_state(child, X86OS_PROCESS_WAITING, 250U) != 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }

    x86os_ipc_message_t message;
    ipc_message_prepare(&message);
    if (x86os_ipc_send(
            handle, (const x86os_ipc_message_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_ipc_receive(
            handle, (x86os_ipc_message_t*)(uintptr_t)0x1000U) != -14) {
        (void)x86os_ipc_close(handle);
        return -1;
    }
    message.version = X86OS_IPC_MESSAGE_VERSION + 1U;
    if (x86os_ipc_send(handle, &message) >= 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }
    ipc_message_prepare(&message);
    message.length = X86OS_IPC_MAX_MESSAGE_SIZE + 1U;
    if (x86os_ipc_send(handle, &message) >= 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }

    ipc_message_set(&message, "PING");
    if (x86os_ipc_send(handle, &message) != 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }
    ipc_message_prepare(&message);
    if (x86os_ipc_receive(handle, &message) != 0 ||
        !ipc_message_is(&message, "PONG") ||
        wait_for_ipc_child(child, 53) != 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }

    x86os_ipc_handle_t stale = handle;
    if (x86os_ipc_close(handle) != 0 || x86os_ipc_create(&handle) != 0 ||
        handle == stale) return -1;
    ipc_message_set(&message, "STALE");
    if (x86os_ipc_send(stale, &message) >= 0 ||
        x86os_ipc_close(handle) != 0) return -1;

    /* Per-process capability admission is fixed and fail-closed. */
    x86os_ipc_handle_t quota[X86OS_IPC_MAX_CAPABILITIES_PER_PROCESS];
    for (size_t index = 0;
         index < X86OS_IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        quota[index] = X86OS_IPC_INVALID_HANDLE;
        if (x86os_ipc_create(&quota[index]) != 0) return -1;
    }
    x86os_ipc_handle_t excess = X86OS_IPC_INVALID_HANDLE;
    if (x86os_ipc_create(&excess) >= 0) return -1;
    for (size_t index = 0;
         index < X86OS_IPC_MAX_CAPABILITIES_PER_PROCESS; ++index) {
        if (x86os_ipc_close(quota[index]) != 0) return -1;
    }

    /* Destroying an endpoint must wake an already blocked peer. */
    if (x86os_ipc_create(&handle) != 0) return -1;
    child = spawn_ipc_child("IPC_WAIT_CLOSE", handle);
    if (child <= 0 ||
        wait_for_process_state(child, X86OS_PROCESS_WAITING, 250U) != 0 ||
        x86os_ipc_close(handle) != 0 ||
        wait_for_ipc_child(child, 55) != 0) return -1;

    /* A peer exit revokes its inherited capability and wakes the owner. */
    if (x86os_ipc_create(&handle) != 0) return -1;
    child = spawn_ipc_child("IPC_EXIT", handle);
    if (child <= 0) {
        (void)x86os_ipc_close(handle);
        return -1;
    }
    ipc_message_prepare(&message);
    if (x86os_ipc_receive(handle, &message) >= 0 ||
        wait_for_ipc_child(child, 54) != 0 ||
        x86os_ipc_close(handle) != 0 ||
        x86os_memory_stats(&after) != 0 ||
        after.allocated_frame_bytes != before.allocated_frame_bytes ||
        after.heap_used_bytes != before.heap_used_bytes) return -1;
    return 0;
}

static int test_diagnostic_service(void) {
    x86os_network_probe_stats_t stats_before;
    x86os_network_probe_stats_t stats_after;
    x86os_reist_arp_binding_t unauthorized_binding = {
        .version = X86OS_REIST_ARP_BINDING_VERSION,
        .struct_size = sizeof(unauthorized_binding),
        .probe_id = 1U,
        .ip = 0x0A000202U,
        .mac = {2U, 1U, 2U, 3U, 4U, 5U},
    };
    x86os_reist_arp_reply_t unauthorized_reply = {
        .version = X86OS_REIST_ARP_REPLY_VERSION,
        .struct_size = sizeof(unauthorized_reply),
        .request_id = 1U,
        .target_ip = 0x0A000203U,
        .target_mac = {2U, 6U, 7U, 8U, 9U, 10U},
    };
    if (x86os_network_probe_stats(
            (x86os_network_probe_stats_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_reist_commit_arp_binding(
            (const x86os_reist_arp_binding_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_reist_commit_arp_binding(&unauthorized_binding) != -13 ||
        x86os_reist_send_arp_reply(
            (const x86os_reist_arp_reply_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_reist_send_arp_reply(&unauthorized_reply) != -13 ||
        x86os_network_probe_stats(&stats_before) != 0 ||
        stats_before.version != X86OS_NETWORK_PROBE_STATS_VERSION ||
        stats_before.struct_size != sizeof(stats_before) ||
        stats_before.reserved != 0U) return -1;
    x86os_ipc_handle_t handle = X86OS_IPC_INVALID_HANDLE;
    if (x86os_network_probe_id((uint32_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_service_connect(X86OS_SERVICE_DIAGNOSTIC,
            (x86os_ipc_handle_t*)(uintptr_t)0x1000U) != -14 ||
        x86os_service_connect(0xFFFFFFFFU, &handle) >= 0 ||
        x86os_service_connect(X86OS_SERVICE_DIAGNOSTIC, &handle) != 0 ||
        handle == X86OS_IPC_INVALID_HANDLE) return -1;

    x86os_ipc_message_t message;
    uint32_t request_id = 1U;
    if (service_request_set(&message, request_id, "BADID") != 0) return -1;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
        service_response_is(&message, request_id, "REIST_DIAG_OK"))
        return -1;
    x86os_puts("TEST_STAGE SERVICE_CORRELATION_OK\n");
    ++request_id;
    if (service_request_set(&message, request_id, "DIAG") != 0) return -1;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
        !service_response_is(&message, request_id, "REIST_DIAG_OK"))
        return -1;
    /* Clients receive attenuated SEND/RECEIVE rights, never ownership. */
    if (x86os_ipc_close(handle) >= 0 || x86os_ipc_release(handle) != 0)
        return -1;
    ipc_message_set(&message, "STALE");
    if (x86os_ipc_send_timeout(handle, &message, 0U) >= 0) return -1;

    /* A released single-client slot can be delegated again without leaking
     * capability quota or requiring endpoint destruction. */
    handle = X86OS_IPC_INVALID_HANDLE;
    if (x86os_service_connect(X86OS_SERVICE_DIAGNOSTIC, &handle) != 0)
        return -1;
    ipc_message_set_test_arp_identity(&message);
    message.payload[46] ^= 1U;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 100U) != -110)
        return -1;
    ipc_message_set_test_arp_identity(&message);
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
        !ipc_message_is(&message, "REIST_NET_ARP")) return -1;
    x86os_puts("TEST_STAGE ARP_IDENTITY_OK\n");
    ipc_message_set_test_arp_frame(&message);
    message.payload[22] = 5U;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 100U) != -110)
        return -1;
    x86os_puts("TEST_STAGE ARP_VALIDATION_OK\n");
    ipc_message_set_test_arp_frame(&message);
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
        !ipc_message_is(&message, "REIST_NET_ARP")) return -1;

    ++request_id;
    if (service_request_set(&message, request_id, "NETPROBE") != 0) return -1;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 1000U) != 0) return -1;
    int network_handoff = service_response_is(&message, request_id,
                                               "REIST_NET_ARP");
    if (network_handoff)
        x86os_puts("TEST_STAGE NETWORK_HANDOFF_OK\n");
    else if (!service_response_is(&message, request_id,
                                  "REIST_NET_UNAVAILABLE")) return -1;

    if (!network_handoff) {
        if (x86os_network_probe_stats(&stats_after) != 0 ||
            stats_after.expired < stats_before.expired ||
            stats_after.queue_fallback < stats_before.queue_fallback ||
            stats_after.semantic_reject < stats_before.semantic_reject ||
            x86os_ipc_release(handle) != 0) return -1;
        x86os_puts("TEST_STAGE NETWORK_STATS_OK\n");
        return 0;
    }

    /* The host-side strict NIC gate injects a real ARP request only after this
     * line.  Keep the window finite while allowing slow Windows pipe delivery
     * to synchronize without turning the service path into a polling loop. */
    x86os_puts("TEST_STAGE NETWORK_INJECTION_READY\n");
    if (x86os_sleep_ms(2000U) != 0) return -1;

    /* Hold the owner briefly, fill every bounded queue slot, then let its
     * real ARP probe prove that ingress fails closed to the kernel path. */
    if (x86os_sleep_ms(300U) != 0) return -1;
    ++request_id;
    if (service_request_set(&message, request_id, "NETPRESSURE") != 0)
        return -1;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
        !service_response_is(&message, request_id, "REIST_PRESSURE_READY"))
        return -1;
    for (unsigned int index = 0U; index < X86OS_IPC_QUEUE_DEPTH; ++index) {
        ++request_id;
        if (service_request_set(&message, request_id, "LOAD") != 0)
            return -1;
        if (x86os_ipc_send_timeout(handle, &message, 0U) != 0) return -1;
    }
    if (x86os_sleep_ms(500U) != 0) return -1;
    for (unsigned int index = 0U; index < X86OS_IPC_QUEUE_DEPTH; ++index) {
        ipc_message_prepare(&message);
        uint32_t response_id = request_id - X86OS_IPC_QUEUE_DEPTH + index + 1U;
        if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
            !service_response_is(&message, response_id,
                                 "REIST_DIAG_INVALID")) return -1;
    }
    x86os_puts("TEST_STAGE NETWORK_PRESSURE_OK\n");
    if (x86os_network_probe_stats(&stats_after) != 0 ||
        stats_after.expired < stats_before.expired ||
        stats_after.queue_fallback <= stats_before.queue_fallback ||
        stats_after.semantic_reject < stats_before.semantic_reject)
        return -1;
    x86os_puts("TEST_STAGE NETWORK_STATS_OK\n");

    if (x86os_sleep_ms(300U) != 0) return -1;
    ++request_id;
    if (service_request_set(&message, request_id, "NETCRASH") != 0)
        return -1;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 1000U) >= 0) return -1;

    handle = X86OS_IPC_INVALID_HANDLE;
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        if (x86os_service_connect(X86OS_SERVICE_DIAGNOSTIC, &handle) == 0)
            break;
        if (x86os_sleep_ms(20U) != 0) return -1;
    }
    if (handle == X86OS_IPC_INVALID_HANDLE) return -1;
    ++request_id;
    if (service_request_set(&message, request_id, "DIAG") != 0) return -1;
    if (x86os_ipc_send_timeout(handle, &message, 250U) != 0) return -1;
    ipc_message_prepare(&message);
    if (x86os_ipc_receive_timeout(handle, &message, 500U) != 0 ||
        !service_response_is(&message, request_id, "REIST_DIAG_OK") ||
        x86os_ipc_release(handle) != 0) return -1;
    x86os_puts("TEST_STAGE NETWORK_RECOVERY_OK\n");
    return 0;
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
    if (quick_pid <= 0 ||
        wait_for_process_state(quick_pid, X86OS_PROCESS_ZOMBIE, 250U) != 0) {
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
    /* Worker, probe, shell and GTEST occupy four slots. Three ambient slots
     * remain while the final slot stays reserved for supervised restart. */
    int children[3];
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

int main(int argc, char **argv) {
    if (argc == 3 && text_equal(argv[1], "IPC_ECHO"))
        return ipc_child_main(argv[1], argv[2]);
    if (argc == 3 && text_equal(argv[1], "IPC_WAIT_CLOSE"))
        return ipc_child_main(argv[1], argv[2]);
    if (argc == 3 && text_equal(argv[1], "IPC_EXIT"))
        return ipc_child_main(argv[1], argv[2]);

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

    if (test_ipc_capabilities() != 0) {
        x86os_puts("TEST_FAIL IPC\n");
        return 5;
    }
    x86os_puts("TEST_STAGE IPC_OK\n");

    if (test_diagnostic_service() != 0) {
        x86os_puts("TEST_FAIL DIAGNOSTIC_SERVICE\n");
        return 6;
    }
    x86os_puts("TEST_STAGE DIAGNOSTIC_SERVICE_OK\n");
    x86os_puts("TEST_STAGE NETWORK_PARSER_OK\n");

    if (test_task_capacity_and_parenting() != 0) {
        x86os_puts("TEST_FAIL TASK_CAPACITY\n");
        return 7;
    }
    x86os_puts("TEST_STAGE TASK_CAPACITY_OK\n");
    x86os_puts("TEST_STAGE REIST_PROGRESS_OK\n");

    if (wait_for_expected("FAULTDE.PRG", 128) != 0 ||
        wait_for_expected("FAULTUD.PRG", 134) != 0 ||
        wait_for_expected("FAULTPF.PRG", 142) != 0 ||
        wait_for_expected("FAULTSTK.PRG", 142) != 0) {
        x86os_puts("TEST_FAIL EXCEPTIONS\n");
        return 8;
    }
    x86os_puts("TEST_STAGE EXCEPTIONS_OK\n");
    x86os_puts("TEST_OK\n");
    return 0;
}
