/**
 * @file userspace/programs/guest_test.c
 * @brief Führt deterministische Gast-Regressionstests aus.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include <stdbool.h>

#include "x86os.h"
#include "reist/vfs_file_client.h"

#define WAIT_STRESS_ITERATIONS 64
#define GUEST_TEST_TASK_CAPACITY_LIMIT 32U
#define OPEN_FLAGS_DESCRIPTOR_CAPACITY 8

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

static int test_standard_descriptors(void) {
    static const char stdout_marker[] = "STDOUT_FD_OK\n";
    static const char stderr_marker[] = "STDERR_FD_OK\n";
    static const char independent_marker[] =
        "STDERR_AFTER_STDOUT_CLOSE_OK\n";
    char byte = 0;

    if (x86os_read(X86OS_STDIN_FILENO, &byte, 0U) != 0) return -1;
    if (x86os_write(X86OS_STDIN_FILENO, &byte, 0U) != -9) return -2;
    if (x86os_read(X86OS_STDOUT_FILENO, &byte, 0U) != -9) return -3;
    if (x86os_read(X86OS_STDERR_FILENO, &byte, 0U) != -9) return -4;
    if (x86os_write(X86OS_STDOUT_FILENO, stdout_marker,
                    sizeof(stdout_marker) - 1U) !=
        (int)(sizeof(stdout_marker) - 1U)) return -5;
    if (x86os_write(X86OS_STDERR_FILENO, stderr_marker,
                    sizeof(stderr_marker) - 1U) !=
        (int)(sizeof(stderr_marker) - 1U)) return -6;

    int descriptor = x86os_open("/readme.txt");
    if (descriptor != 3 || x86os_close(descriptor) != 0) return -7;

    if (x86os_close(X86OS_STDOUT_FILENO) != 0) return -8;
    if (x86os_close(X86OS_STDOUT_FILENO) != -9) return -9;
    if (x86os_write(X86OS_STDOUT_FILENO, stdout_marker,
                    sizeof(stdout_marker) - 1U) != -9) return -10;
    if (x86os_write(X86OS_STDERR_FILENO, independent_marker,
                    sizeof(independent_marker) - 1U) !=
        (int)(sizeof(independent_marker) - 1U)) return -11;
    return 0;
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

static int vfs_delegation_child_main(void) {
    if (x86os_sleep_ms(2000U) != 0) return 80;
    for (uint32_t delegated = 0U; delegated < 4U; ++delegated) {
        reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
        for (uint32_t attempt = 0U; attempt < 100U; ++attempt) {
            int status = reist_vfs_file_adopt(
                REIST_VFS_FILE_DEFAULT_TIMEOUT_MS, &handle);
            if (status == 0) break;
            if (status != -11 || x86os_sleep_ms(10U) != 0) return 81;
        }
        if (handle == REIST_VFS_FILE_INVALID_HANDLE) return 82;
        uint32_t rights = 0U;
        uint8_t byte = 0U;
        x86os_file_info_t info;
        uint32_t offset = 0U;
        if (reist_vfs_file_rights(handle, &rights) != 0 ||
            rights != REIST_VFS_FILE_RIGHT_READ ||
            reist_vfs_file_read(handle, &byte, 1U) != 1 || byte == 0U ||
            reist_vfs_file_fstat(handle, &info) != -13 ||
            reist_vfs_file_seek(handle, 0, REIST_VFS_SEEK_SET, &offset) !=
                -13 || reist_vfs_file_close(handle) != 0) return 83;
    }
    reist_vfs_file_handle_t duplicate = REIST_VFS_FILE_INVALID_HANDLE;
    return reist_vfs_file_adopt(REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
                                &duplicate) == -11 ? 57 : 84;
}

static int vfs_delegation_fail(uint32_t stage, int status) {
    x86os_puts("VFS_DELEGATION_FAIL stage=");
    x86os_print_number((int)stage);
    x86os_puts(" status=");
    x86os_print_number(status);
    x86os_putchar('\n');
    return -1;
}

static int vfs_delegation_expiry_child_main(void) {
    if (x86os_sleep_ms(7000U) != 0) return 85;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_adopt(
        REIST_VFS_FILE_DEFAULT_TIMEOUT_MS, &handle);
    if (status == -11) return 58;
    (void)vfs_delegation_fail(11U, status);
    if (handle != REIST_VFS_FILE_INVALID_HANDLE)
        (void)reist_vfs_file_close(handle);
    return 86;
}

static int bytes_equal(const char *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return 1;
}

static int guest_vfs_shadow_stat(const char *path, x86os_file_info_t *info,
                                 uint32_t timeout_ms, uint32_t operation) {
    if (path == NULL || info == NULL || timeout_ms == 0U ||
        timeout_ms > 60000U ||
        (operation != X86OS_VFS_SHADOW_STAT &&
         operation != X86OS_VFS_SHADOW_FAT32_STAT &&
         operation != X86OS_VFS_SHADOW_FAT_STAT &&
         operation != X86OS_VFS_SHADOW_FAT_STAT_AUTHORITY &&
         operation != X86OS_VFS_SHADOW_FS_STAT_AUTHORITY)) return -22;
    uint32_t length = 0U;
    while (length < X86OS_VFS_SHADOW_PATH_CAPACITY && path[length] != '\0')
        ++length;
    if (length == 0U || length >= X86OS_VFS_SHADOW_PATH_CAPACITY ||
        path[0] != '/') return -22;

    x86os_vfs_shadow_frame_t frame;
    uint8_t *frame_bytes = (uint8_t *)&frame;
    for (uint32_t index = 0U; index < sizeof(frame); ++index)
        frame_bytes[index] = 0U;
    frame.version = X86OS_VFS_SHADOW_FRAME_VERSION;
    frame.struct_size = sizeof(frame);
    frame.operation = operation;
    frame.path_length = length;
    for (uint32_t index = 0U; index <= length; ++index)
        frame.path[index] = path[index];
    x86os_storage_submit_t request = {
        X86OS_STORAGE_REQUEST_VERSION, sizeof(request),
        X86OS_STORAGE_VFS_SHADOW_STAT, 0U, 0U, sizeof(frame), timeout_ms,
    };
    x86os_storage_handle_t handle = 0U;
    int status = x86os_storage_submit(&request, &frame, &handle);
    if (status != 0 || handle == 0U) return status != 0 ? status : -5;

    uint64_t now = 0U;
    if (x86os_monotonic_ms(&now) != 0) return -5;
    uint64_t deadline = UINT64_MAX - now < timeout_ms
        ? UINT64_MAX : now + timeout_ms;
    int32_t service_result = -5;
    for (;;) {
        status = x86os_storage_collect(handle, &service_result, &frame);
        if (status == 0) break;
        if (status != -11 || x86os_monotonic_ms(&now) != 0) return status;
        if (now >= deadline) return -110;
        if (x86os_sleep_ms(1U) != 0) return -5;
    }
    if (service_result != 0) return service_result;
    if (frame.version != X86OS_VFS_SHADOW_FRAME_VERSION ||
        frame.struct_size != sizeof(frame) ||
        frame.operation != operation || frame.flags != 0U ||
        frame.path_length != length || frame.path[length] != '\0') return -84;
    for (uint32_t index = 0U; index < length; ++index)
        if (frame.path[index] != path[index]) return -84;
    for (uint32_t index = 0U; index < 5U; ++index)
        if (frame.reserved[index] != 0U) return -84;
    if (frame.result == 0 && frame.info.type != X86OS_FILE &&
        frame.info.type != X86OS_DIRECTORY) return -84;
    if (frame.result == 0) {
        for (uint32_t index = 0U; index < sizeof(info->name); ++index)
            info->name[index] = frame.info.name[index];
        info->type = frame.info.type;
        info->size = frame.info.size;
        info->create_time = frame.info.create_time;
        info->modify_time = frame.info.modify_time;
        info->access_time = frame.info.access_time;
    }
    return frame.result;
}

static int test_open_flags(void) {
    static const char path[] = "OPENFLG.TMP";
    static const char full[] = "BASE+END";
    static const char reset[] = "RESET";
    static const char absent[] = "NOFILE.TMP";
    int held[OPEN_FLAGS_DESCRIPTOR_CAPACITY];
    char actual[sizeof(full) - 1U];
    char byte = 0;

    (void)x86os_unlink(path);
    (void)x86os_unlink(absent);
    int descriptor = x86os_open_flags(
        path, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor != 3 ||
        x86os_write(descriptor, full, 4U) != 4 ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 || x86os_write(descriptor, &byte, 1U) !=
            -REIST_EBADF ||
        x86os_read(descriptor, actual, 4U) != 4 ||
        !bytes_equal(actual, full, 4U) || x86os_close(descriptor) != 0)
        goto failed;

    descriptor = x86os_open_flags(
        path, X86OS_O_WRONLY | X86OS_O_APPEND);
    if (descriptor < 0 || x86os_read(descriptor, &byte, 1U) !=
            -REIST_EBADF ||
        x86os_write(descriptor, full + 4U, 1U) != 1 ||
        x86os_write(descriptor, full + 5U, 3U) != 3 ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;

    if (x86os_open_flags(path, X86OS_O_RDONLY | X86OS_O_APPEND) !=
            -REIST_EINVAL ||
        x86os_open_flags(path, X86OS_O_ACCMODE) != -REIST_EINVAL ||
        x86os_open_flags(path, 0x80000000U) != -REIST_EINVAL ||
        x86os_open_flags(path, X86OS_O_RDONLY | X86OS_O_TRUNC) !=
            -REIST_EINVAL) goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 ||
        x86os_read(descriptor, actual, sizeof(actual)) !=
            (int)sizeof(actual) ||
        !bytes_equal(actual, full, sizeof(actual)) ||
        x86os_close(descriptor) != 0) goto failed;

    descriptor = x86os_open_flags(
        path, X86OS_O_WRONLY | X86OS_O_TRUNC);
    x86os_file_info_t info;
    if (descriptor < 0 || x86os_stat(path, &info) != 0 || info.size != 0U ||
        x86os_write(descriptor, reset, sizeof(reset) - 1U) !=
            (int)(sizeof(reset) - 1U) ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;
    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 ||
        x86os_read(descriptor, actual, sizeof(reset) - 1U) !=
            (int)(sizeof(reset) - 1U) ||
        !bytes_equal(actual, reset, sizeof(reset) - 1U) ||
        x86os_read(descriptor, &byte, 1U) != 0 ||
        x86os_close(descriptor) != 0) goto failed;

    for (int index = 0; index < OPEN_FLAGS_DESCRIPTOR_CAPACITY; ++index) {
        held[index] = x86os_open_flags(path, X86OS_O_RDONLY);
        if (held[index] < 0) {
            while (index-- > 0) (void)x86os_close(held[index]);
            goto failed;
        }
    }
    if (x86os_open_flags(absent, X86OS_O_CREAT | X86OS_O_RDWR) !=
            -REIST_EMFILE) {
        for (int index = 0; index < OPEN_FLAGS_DESCRIPTOR_CAPACITY; ++index)
            (void)x86os_close(held[index]);
        goto failed;
    }
    for (int index = 0; index < OPEN_FLAGS_DESCRIPTOR_CAPACITY; ++index) {
        if (x86os_close(held[index]) != 0) goto failed;
    }
    if (x86os_stat(absent, &info) == 0 || x86os_unlink(path) != 0) goto failed;
    return 0;

failed:
    (void)x86os_unlink(absent);
    (void)x86os_unlink(path);
    return -1;
}

static int test_descriptor_seek_fstat(void) {
    static const char path[] = "SEEKFD.TMP";
    static const char initial[] = "ABCDE";
    static const char expected[] = {
        'A', 'B', 'C', 'D', 'E', 0, 0, 0, 'Z', 'Q'
    };
    char actual[sizeof(expected)];
    char partial[2];
    char byte = 0;
    x86os_file_info_t descriptor_info;
    x86os_file_info_t path_info;

    (void)x86os_unlink(path);
    int descriptor = x86os_open_flags(path, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 ||
        x86os_write(descriptor, initial, sizeof(initial) - 1U) !=
            (int)(sizeof(initial) - 1U) ||
        x86os_fstat(descriptor, &descriptor_info) != 0 ||
        x86os_stat(path, &path_info) != 0 ||
        !bytes_equal((const char*)&descriptor_info, (const char*)&path_info,
                     sizeof(path_info)) ||
        descriptor_info.type != X86OS_FILE ||
        descriptor_info.size != sizeof(initial) - 1U) goto failed;

    if (x86os_lseek(descriptor, 1, X86OS_SEEK_SET) != 1 ||
        x86os_read(descriptor, partial, sizeof(partial)) != 2 ||
        partial[0] != 'B' || partial[1] != 'C' ||
        x86os_lseek(descriptor, -1, X86OS_SEEK_CUR) != 2 ||
        x86os_read(descriptor, &byte, 1U) != 1 || byte != 'C' ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_END) != 5 ||
        x86os_read(descriptor, &byte, 1U) != 0) goto failed;

    if (x86os_lseek(descriptor, -1, X86OS_SEEK_SET) != -REIST_EINVAL ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != 5 ||
        x86os_lseek(descriptor, 0, 3U) != -REIST_EINVAL ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != 5 ||
        x86os_lseek(descriptor, INT32_MAX, X86OS_SEEK_SET) != INT32_MAX ||
        x86os_lseek(descriptor, 1, X86OS_SEEK_CUR) != -REIST_EOVERFLOW ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != INT32_MAX ||
        x86os_lseek(descriptor, 8, X86OS_SEEK_SET) != 8 ||
        x86os_write(descriptor, "Z", 1U) != 1 ||
        x86os_fsync(descriptor) != 0 ||
        x86os_fstat(descriptor, &descriptor_info) != 0 ||
        descriptor_info.size != 9U || x86os_close(descriptor) != 0)
        goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_WRONLY | X86OS_O_APPEND);
    if (descriptor < 0 ||
        x86os_fstat(descriptor, &descriptor_info) != 0 ||
        descriptor_info.size != 9U ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_SET) != 0 ||
        x86os_write(descriptor, "Q", 1U) != 1 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != 10 ||
        x86os_fstat(descriptor, &descriptor_info) != 0 ||
        descriptor_info.size != sizeof(expected) ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 ||
        x86os_fstat(descriptor,
                    (x86os_file_info_t*)(uintptr_t)0x1000U) !=
            -REIST_EFAULT ||
        x86os_read(descriptor, actual, sizeof(actual)) !=
            (int)sizeof(actual) ||
        !bytes_equal(actual, expected, sizeof(expected)) ||
        x86os_read(descriptor, &byte, 1U) != 0 ||
        x86os_fstat(descriptor, &descriptor_info) != 0 ||
        x86os_stat(path, &path_info) != 0 ||
        !bytes_equal((const char*)&descriptor_info, (const char*)&path_info,
                     sizeof(path_info)) ||
        x86os_close(descriptor) != 0 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_SET) != -REIST_EBADF ||
        x86os_lseek(X86OS_STDERR_FILENO, 0, X86OS_SEEK_SET) !=
            -REIST_ESPIPE ||
        x86os_fstat(X86OS_STDERR_FILENO, &descriptor_info) !=
            -REIST_ESPIPE ||
        x86os_unlink(path) != 0) goto failed;
    return 0;

failed:
    if (descriptor >= 0) (void)x86os_close(descriptor);
    (void)x86os_unlink(path);
    return -1;
}

static int test_ftruncate(void) {
    static const char path[] = "FTRUNC.TMP";
    static uint8_t original[700];
    static uint8_t actual[700];
    x86os_file_info_t info;
    for (uint32_t index = 0U; index < sizeof(original); ++index)
        original[index] = (uint8_t)('A' + index % 23U);

    (void)x86os_unlink(path);
    int descriptor = x86os_open_flags(path, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 ||
        x86os_write(descriptor, original, sizeof(original)) !=
            (int)sizeof(original) ||
        x86os_lseek(descriptor, 600, X86OS_SEEK_SET) != 600 ||
        x86os_ftruncate(descriptor, 200U) != 0 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != 600 ||
        x86os_fstat(descriptor, &info) != 0 || info.size != 200U ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_SET) != 0 ||
        x86os_read(descriptor, actual, 200U) != 200 ||
        !bytes_equal((const char*)actual, (const char*)original, 200U) ||
        x86os_read(descriptor, actual, 1U) != 0) goto failed;

    if (x86os_ftruncate(descriptor, sizeof(original)) != 0 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != 200 ||
        x86os_fstat(descriptor, &info) != 0 ||
        info.size != sizeof(original) ||
        x86os_read(descriptor, actual, sizeof(actual) - 200U) !=
            (int)(sizeof(actual) - 200U)) goto failed;
    for (uint32_t index = 0U; index < sizeof(actual) - 200U; ++index)
        if (actual[index] != 0U) goto failed;

    if (x86os_ftruncate(descriptor, sizeof(original)) != 0 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) !=
            (int32_t)sizeof(original) ||
        x86os_ftruncate(descriptor, 0U) != 0 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) !=
            (int32_t)sizeof(original) ||
        x86os_fstat(descriptor, &info) != 0 || info.size != 0U ||
        x86os_close(descriptor) != 0) goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 || x86os_ftruncate(descriptor, 1U) != -REIST_EBADF ||
        x86os_close(descriptor) != 0 ||
        x86os_ftruncate(descriptor, 1U) != -REIST_EBADF ||
        x86os_ftruncate(X86OS_STDERR_FILENO, 1U) != -REIST_EINVAL)
        goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_WRONLY);
    if (descriptor < 0 || x86os_ftruncate(descriptor, 513U) != 0 ||
        x86os_lseek(descriptor, 0, X86OS_SEEK_CUR) != 0 ||
        x86os_fstat(descriptor, &info) != 0 || info.size != 513U ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;
    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 || x86os_read(descriptor, actual, 513U) != 513)
        goto failed;
    for (uint32_t index = 0U; index < 513U; ++index)
        if (actual[index] != 0U) goto failed;
    if (x86os_close(descriptor) != 0 || x86os_unlink(path) != 0) goto failed;
    return 0;

failed:
    if (descriptor >= 0) (void)x86os_close(descriptor);
    (void)x86os_unlink(path);
    return -1;
}

static int test_fat_timestamps(void) {
    static const char path[] = "FATTIME.TMP";
    static const char payload[] = "timestamp";
    x86os_file_info_t created;
    x86os_file_info_t written;
    x86os_file_info_t touched;
    x86os_file_info_t observed;
    char actual[sizeof(payload) - 1U];

    (void)x86os_unlink(path);
    int descriptor = x86os_open_flags(path, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 || x86os_fstat(descriptor, &created) != 0 ||
        created.create_time == 0U || created.modify_time == 0U ||
        created.access_time == 0U ||
        x86os_write(descriptor, payload, sizeof(payload) - 1U) !=
            (int)(sizeof(payload) - 1U) ||
        x86os_fstat(descriptor, &written) != 0 ||
        written.create_time != created.create_time ||
        written.modify_time < created.modify_time ||
        written.access_time != created.access_time ||
        written.size != sizeof(payload) - 1U ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;
    descriptor = -1;

    if (x86os_touch(path) != 0 || x86os_stat(path, &touched) != 0 ||
        touched.create_time != created.create_time ||
        touched.modify_time < written.modify_time ||
        touched.access_time == 0U || touched.size != written.size)
        goto failed;

    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 ||
        x86os_read(descriptor, actual, sizeof(actual)) !=
            (int)sizeof(actual) ||
        !bytes_equal(actual, payload, sizeof(actual)) ||
        x86os_fstat(descriptor, &observed) != 0 ||
        observed.create_time != touched.create_time ||
        observed.modify_time != touched.modify_time ||
        observed.access_time != touched.access_time ||
        x86os_close(descriptor) != 0 || x86os_unlink(path) != 0)
        goto failed;
    return 0;

failed:
    if (descriptor >= 0) (void)x86os_close(descriptor);
    (void)x86os_unlink(path);
    return -1;
}

static int test_vfat_utf8(void) {
    static const char path[] =
        "U-\xC3\xBC-\xF0\x9F\x9A\x80.TMP";
    static const char payload[] = "utf8-vfat";
    x86os_file_info_t info;
    char actual[sizeof(payload) - 1U];

    (void)x86os_unlink(path);
    int descriptor = x86os_open_flags(path, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 ||
        x86os_write(descriptor, payload, sizeof(payload) - 1U) !=
            (int)(sizeof(payload) - 1U) ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;
    descriptor = -1;
    if (x86os_stat(path, &info) != 0 ||
        !bytes_equal(info.name, path, sizeof(path)) ||
        info.size != sizeof(payload) - 1U) goto failed;
    descriptor = x86os_open_flags(path, X86OS_O_RDONLY);
    if (descriptor < 0 ||
        x86os_read(descriptor, actual, sizeof(actual)) !=
            (int)sizeof(actual) ||
        !bytes_equal(actual, payload, sizeof(actual)) ||
        x86os_close(descriptor) != 0 || x86os_unlink(path) != 0)
        goto failed;
    descriptor = -1;

    static const char composed[] = "N-\xC3\x84-\xC3\x9F.TMP";
    static const char alternate[] = "n-A\xCC\x88-ss.tmp";
    (void)x86os_unlink(composed);
    descriptor = x86os_open_flags(composed, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 || x86os_close(descriptor) != 0) goto failed;
    descriptor = -1;
    if (x86os_stat(alternate, &info) != 0 ||
        !bytes_equal(info.name, composed, sizeof(composed))) goto failed;
    descriptor = x86os_open_flags(alternate, X86OS_O_RDONLY);
    if (descriptor < 0 || x86os_close(descriptor) != 0 ||
        x86os_unlink(alternate) != 0) goto failed;

    static const char rename_source[] =
        "Quelle-\xC3\xBC-\xF0\x9F\x9A\x80.tmp";
    static const char rename_target[] =
        "Ziel-\xC3\x84-\xF0\x9F\x8C\x8D.tmp";
    static const char source_payload[] = "unicode LFN replacement";
    static const char target_payload[] = "old unicode target";
    (void)x86os_unlink(rename_source);
    (void)x86os_unlink(rename_target);
    descriptor = x86os_open_flags(
        rename_target, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 ||
        x86os_write(descriptor, target_payload, sizeof(target_payload) - 1U) !=
            (int)(sizeof(target_payload) - 1U) ||
        x86os_close(descriptor) != 0) goto failed;
    descriptor = x86os_open_flags(
        rename_source, X86OS_O_CREAT | X86OS_O_RDWR);
    if (descriptor < 0 ||
        x86os_write(descriptor, source_payload, sizeof(source_payload) - 1U) !=
            (int)(sizeof(source_payload) - 1U) ||
        x86os_fsync(descriptor) != 0 || x86os_close(descriptor) != 0)
        goto failed;
    if (x86os_rename(rename_source, rename_target) != 0) {
        x86os_puts("VFAT_LFN_FAIL rename\n");
        goto failed;
    }
    int source_stat = x86os_stat(rename_source, &info);
    int target_stat = x86os_stat(rename_target, &info);
    if (source_stat == 0 || target_stat != 0 ||
        info.size != sizeof(source_payload) - 1U) {
        x86os_puts("VFAT_LFN_FAIL stat source=");
        x86os_print_number(source_stat);
        x86os_puts(" target=");
        x86os_print_number(target_stat);
        x86os_puts(" size=");
        x86os_print_number((int)info.size);
        x86os_putchar('\n');
        goto failed;
    }
    descriptor = x86os_open_flags(rename_target, X86OS_O_RDONLY);
    char renamed_payload[sizeof(source_payload) - 1U];
    if (descriptor < 0 ||
        x86os_read(descriptor, renamed_payload, sizeof(renamed_payload)) !=
            (int)sizeof(renamed_payload) ||
        !bytes_equal(renamed_payload, source_payload,
                     sizeof(renamed_payload)) ||
        x86os_close(descriptor) != 0 || x86os_unlink(rename_target) != 0) {
        x86os_puts("VFAT_LFN_FAIL read\n");
        goto failed;
    }
    x86os_puts("VFAT_LFN_REPLACE_OK\n");
    return 0;

failed:
    if (descriptor >= 0) (void)x86os_close(descriptor);
    (void)x86os_unlink(path);
    (void)x86os_unlink("N-\xC3\x84-\xC3\x9F.TMP");
    (void)x86os_unlink("Quelle-\xC3\xBC-\xF0\x9F\x9A\x80.tmp");
    (void)x86os_unlink("Ziel-\xC3\x84-\xF0\x9F\x8C\x8D.tmp");
    return -1;
}

static int test_unicode_raster(void) {
    static const char program[] = "/usr/gui/bin/desktop.prg";
    static const char probe[] = "--unicode-probe";
    const char *arguments[] = {program, probe};
    int pid = x86os_spawnv(program, 2, arguments);
    if (pid <= 0) return -1;
    int status = -1;
    return x86os_wait(pid, &status) == pid && status == 0 ? 0 : -1;
}

static int test_vfs_readonly_walkers(void) {
    static const char tree_program[] = "/bin/tree.prg";
    static const char find_program[] = "/bin/find.prg";
    static const char root[] = "/htdocs";
    static const char match[] = "about.txt";
    const char *tree_arguments[] = {tree_program, root};
    int pid = x86os_spawnv(tree_program, 2, tree_arguments);
    int status = -1;
    if (pid <= 0 || x86os_wait(pid, &status) != pid || status != 0)
        return -1;
    const char *find_arguments[] = {find_program, root, match};
    pid = x86os_spawnv(find_program, 3, find_arguments);
    status = -1;
    return pid > 0 && x86os_wait(pid, &status) == pid && status == 0
        ? 0 : -1;
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
    x86os_file_info_t shadow_info;
    x86os_file_info_t parser_info;
    x86os_file_info_t authority_info;
    x86os_file_info_t filesystem_info;
    int shadow_stat_result = guest_vfs_shadow_stat(
        "/GUEST.TMP", &shadow_info, 1000U, X86OS_VFS_SHADOW_STAT);
    int parser_stat_result = guest_vfs_shadow_stat(
        "/GUEST.TMP", &parser_info, 1000U, X86OS_VFS_SHADOW_FAT32_STAT);
    int authority_stat_result = guest_vfs_shadow_stat(
        "/GUEST.TMP", &authority_info, 1000U,
        X86OS_VFS_SHADOW_FAT_STAT_AUTHORITY);
    int filesystem_stat_result = guest_vfs_shadow_stat(
        "/GUEST.TMP", &filesystem_info, 1000U,
        X86OS_VFS_SHADOW_FS_STAT_AUTHORITY);
    if (amount != (int)sizeof(actual) || eof != 0 || close_result != 0 ||
        stat_result != 0 || shadow_stat_result != 0 || parser_stat_result != 0 ||
        authority_stat_result != 0 || filesystem_stat_result != 0 ||
        !bytes_equal((const char *)&info, (const char *)&shadow_info,
                     sizeof(info)) ||
        !bytes_equal((const char *)&info, (const char *)&parser_info,
                     sizeof(info)) ||
        !bytes_equal((const char *)&info, (const char *)&authority_info,
                     sizeof(info)) ||
        !bytes_equal((const char *)&info, (const char *)&filesystem_info,
                     sizeof(info)) || info.type != X86OS_FILE ||
        info.size != sizeof(expected) ||
        !bytes_equal(actual, expected, sizeof(actual))) {
        (void)x86os_unlink(path);
        return -1;
    }

    const char *stat_arguments[] = {"/bin/stat.prg", "/GUEST.TMP"};
    int stat_pid = x86os_spawnv(
        stat_arguments[0], 2, stat_arguments);
    int stat_status = -1;
    if (stat_pid <= 0 || x86os_wait(stat_pid, &stat_status) != stat_pid ||
        stat_status != 0) {
        (void)x86os_unlink(path);
        return -1;
    }
    const char *cat_arguments[] = {"/bin/cat.prg", "/README.TXT"};
    int cat_pid = x86os_spawnv(cat_arguments[0], 2, cat_arguments);
    int cat_status = -1;
    if (cat_pid <= 0 || x86os_wait(cat_pid, &cat_status) != cat_pid ||
        cat_status != 0) {
        (void)x86os_unlink(path);
        return -1;
    }
    const char *ls_arguments[] = {"/bin/ls.prg", "/"};
    int ls_pid = x86os_spawnv(ls_arguments[0], 2, ls_arguments);
    int ls_status = -1;
    if (ls_pid <= 0 || x86os_wait(ls_pid, &ls_status) != ls_pid ||
        ls_status != 0) {
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

static int test_open_namespace_locks(void) {
    static const char target[] = "OPNLCK.TMP";
    static const char target_alias[] = "opnlck.tmp";
    static const char source[] = "OPNSRC.TMP";
    static const char moved[] = "OPNMOV.TMP";
    static const char unrelated[] = "OPNOTHR.TMP";
    static const char original[] = "open target";
    static const char replacement[] = "replacement";
    int held = -1;
    int descriptor = -1;

    (void)x86os_unlink(target);
    (void)x86os_unlink(source);
    (void)x86os_unlink(moved);
    (void)x86os_unlink(unrelated);
    descriptor = x86os_create(target);
    if (descriptor < 0 ||
        x86os_write(descriptor, original, sizeof(original) - 1U) !=
            (int)(sizeof(original) - 1U) ||
        x86os_close(descriptor) != 0) goto failed;
    descriptor = -1;
    descriptor = x86os_create(source);
    if (descriptor < 0 ||
        x86os_write(descriptor, replacement, sizeof(replacement) - 1U) !=
            (int)(sizeof(replacement) - 1U) ||
        x86os_close(descriptor) != 0) goto failed;
    descriptor = -1;
    descriptor = x86os_create(unrelated);
    if (descriptor < 0 || x86os_close(descriptor) != 0) goto failed;
    descriptor = -1;

    held = x86os_open_flags(target, X86OS_O_RDWR);
    if (held < 0 || x86os_unlink(target_alias) == 0 ||
        x86os_rename(target_alias, moved) == 0 ||
        x86os_rename(source, target_alias) == 0 ||
        x86os_unlink(unrelated) != 0) goto failed;

    char observed[sizeof(original) - 1U];
    x86os_file_info_t info;
    if (x86os_fstat(held, &info) != 0 || info.size != sizeof(original) - 1U ||
        x86os_read(held, observed, sizeof(observed)) != (int)sizeof(observed) ||
        !bytes_equal(observed, original, sizeof(observed)) ||
        x86os_close(held) != 0) goto failed;
    held = -1;

    if (x86os_rename(source, target) != 0 ||
        x86os_stat(source, &info) == 0) goto failed;
    descriptor = x86os_open(target);
    char replaced[sizeof(replacement) - 1U];
    if (descriptor < 0 ||
        x86os_read(descriptor, replaced, sizeof(replaced)) !=
            (int)sizeof(replaced) ||
        !bytes_equal(replaced, replacement, sizeof(replaced)) ||
        x86os_close(descriptor) != 0 || x86os_unlink(target) != 0)
        goto failed;
    return 0;

failed:
    if (held >= 0) (void)x86os_close(held);
    if (descriptor >= 0) (void)x86os_close(descriptor);
    (void)x86os_unlink(target);
    (void)x86os_unlink(source);
    (void)x86os_unlink(moved);
    (void)x86os_unlink(unrelated);
    return -1;
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

static int test_vfs_object_delegation(void) {
    reist_vfs_file_handle_t source = REIST_VFS_FILE_INVALID_HANDLE;
    if (reist_vfs_file_open_rights(
            "/README.TXT", REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
            REIST_VFS_FILE_RIGHT_ALL, &source) != 0)
        return vfs_delegation_fail(1U, -1);
    const char *arguments[] = {"GTEST.PRG", "VFS_ADOPT"};
    int child = x86os_spawnv("GTEST.PRG", 2, arguments);
    x86os_process_identity_t target;
    if (child <= 0 || x86os_process_identity_of(child, &target) != 0 ||
        target.version != 1U || target.struct_size != sizeof(target) ||
        target.pid != child || target.generation == 0U) {
        if (child > 0) {
            (void)x86os_kill(child);
            int status;
            (void)x86os_wait(child, &status);
        }
        (void)reist_vfs_file_close(source);
        return vfs_delegation_fail(2U, child);
    }
    for (uint32_t index = 0U; index < 4U; ++index)
        {
            int delegated = reist_vfs_file_delegate(
                source, &target, REIST_VFS_FILE_RIGHT_READ);
            if (delegated == 0) continue;
            (void)x86os_kill(child);
            int child_status;
            (void)x86os_wait(child, &child_status);
            (void)reist_vfs_file_close(source);
            return vfs_delegation_fail(3U + index, delegated);
        }
    int excess = reist_vfs_file_delegate(
        source, &target, REIST_VFS_FILE_RIGHT_READ);
    if (excess != -24) {
        (void)x86os_kill(child);
        int child_status;
        (void)x86os_wait(child, &child_status);
        (void)reist_vfs_file_close(source);
        return vfs_delegation_fail(7U, excess);
    }
    x86os_file_info_t info;
    int status = -1;
    int result = reist_vfs_file_fstat(source, &info) == 0 &&
        info.type == X86OS_FILE && x86os_wait(child, &status) == child &&
        status == 57 ? 0 : -1;
    if (reist_vfs_file_close(source) != 0) result = -1;
    if (result != 0) return vfs_delegation_fail(8U, status);

    source = REIST_VFS_FILE_INVALID_HANDLE;
    if (reist_vfs_file_open_rights(
            "/README.TXT", REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
            REIST_VFS_FILE_RIGHT_ALL, &source) != 0)
        return vfs_delegation_fail(9U, -1);
    const char *expiry_arguments[] = {"GTEST.PRG", "VFS_EXPIRE"};
    child = x86os_spawnv("GTEST.PRG", 2, expiry_arguments);
    if (child <= 0 || x86os_process_identity_of(child, &target) != 0 ||
        reist_vfs_file_delegate(source, &target,
                                REIST_VFS_FILE_RIGHT_READ) != 0 ||
        x86os_wait(child, &status) != child || status != 58)
        result = -1;
    if (child > 0 && status != 58) {
        (void)x86os_kill(child);
        (void)x86os_wait(child, &status);
    }
    if (reist_vfs_file_close(source) != 0) result = -1;
    return result == 0 ? 0 : vfs_delegation_fail(10U, status);
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
    /* On NIC-enabled smokes this traverses the complete supervised outgoing
     * ARP path. Headless/no-NIC profiles reject it without affecting GTEST. */
    (void)x86os_network_arp_resolve(0x0A000263U);
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
        x86os_puts("SCHED_FAIL INIT\n");
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
        x86os_puts("SCHED_FAIL QUICK_STATE\n");
        return -1;
    }
    int status = -1;
    if (x86os_wait(quick_pid, &status) != quick_pid || status != 37) {
        x86os_puts("SCHED_FAIL QUICK_WAIT\n");
        return -1;
    }

    uint64_t short_start;
    if (x86os_monotonic_ms(&short_start) != 0 ||
        x86os_sleep_ms(30) != 0 || x86os_monotonic_ms(&now) != 0 ||
        now - short_start < 30U || now - short_start > 2000U) {
        x86os_puts("SCHED_FAIL SHORT_SLEEP\n");
        return -1;
    }

    uint64_t sleeper_start;
    int sleeper_pid = x86os_spawn("SLEEPER.PRG");
    if (sleeper_pid <= 0 || x86os_monotonic_ms(&sleeper_start) != 0) {
        x86os_puts("SCHED_FAIL SLEEP_STATE\n");
        return -1;
    }

    /* Other work must complete while the sleeper is absent from the runnable
     * set.  Waiting for the sleeper then proves its deadline wakeup. */
    if (wait_for_expected("CHILDEX.PRG", 37) != 0 ||
        x86os_monotonic_ms(&now) != 0 || now - sleeper_start >= 400U ||
        x86os_wait(sleeper_pid, &status) != sleeper_pid || status != 41 ||
        x86os_monotonic_ms(&now) != 0 || now - start < 400U) {
        x86os_puts("SCHED_FAIL SLEEP_WAIT\n");
        return -1;
    }

    start = now;
    x86os_delay(25);
    if (x86os_monotonic_ms(&now) != 0 || now - start < 25U) {
        x86os_puts("SCHED_FAIL DELAY\n");
        return -1;
    }

    /* Killing a newly admitted child must revoke its task identity before the
     * slot is reused by the next child. Sleep-queue cancellation remains
     * covered by the deterministic scheduler host harness. */
    sleeper_pid = x86os_spawn("SLEEPER.PRG");
    if (sleeper_pid <= 0) {
        x86os_puts("SCHED_FAIL KILL_SPAWN\n");
        return -1;
    }
    if (x86os_kill(sleeper_pid) != 0) {
        x86os_puts("SCHED_FAIL KILL_CALL\n");
        return -1;
    }
    if (x86os_wait(sleeper_pid, &status) != sleeper_pid || status != 143) {
        x86os_puts("SCHED_FAIL KILL_WAIT\n");
        return -1;
    }
    if (wait_for_expected("CHILDEX.PRG", 37) != 0) {
        x86os_puts("SCHED_FAIL KILL_REUSE\n");
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
        before.heap_largest_free_block > before.heap_free_bytes ||
        before.peak_allocated_frame_bytes < before.allocated_frame_bytes ||
        before.peak_heap_used_bytes < before.heap_used_bytes) {
        return -1;
    }

    const size_t allocation_size = 3U * 4096U + 17U;
    void *allocation = x86os_malloc(allocation_size);
    if (allocation == NULL || x86os_memory_stats(&allocated) != 0 ||
        allocated.allocated_frame_bytes <= before.allocated_frame_bytes ||
        allocated.free_frame_bytes >= before.free_frame_bytes ||
        allocated.peak_allocated_frame_bytes <
            allocated.allocated_frame_bytes ||
        allocated.peak_heap_used_bytes < allocated.heap_used_bytes) {
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
        reclaimed.peak_allocated_frame_bytes <
            allocated.peak_allocated_frame_bytes ||
        reclaimed.peak_heap_used_bytes < allocated.peak_heap_used_bytes ||
        reclaimed.frame_allocation_failures <
            before.frame_allocation_failures ||
        reclaimed.heap_allocation_failures < before.heap_allocation_failures ||
        reclaimed.managed_bytes != reclaimed.reserved_bytes +
            reclaimed.allocated_frame_bytes + reclaimed.free_frame_bytes) {
        return -1;
    }
    return 0;
}

static int test_task_capacity_and_parenting(void) {
    /* Fill exactly the currently reported ambient slots. The fixed array is
     * bounded by the selected research profile and the final supervised slot
     * must remain unavailable to ordinary children. */
    x86os_scheduler_stats_t before;
    if (x86os_scheduler_stats(&before) != 0 ||
        x86os_scheduler_stats(
            (x86os_scheduler_stats_t*)(uintptr_t)0x1000U) != -14 ||
        before.version != X86OS_SCHEDULER_STATS_VERSION ||
        before.struct_size != sizeof(before) ||
        before.task_capacity != GUEST_TEST_TASK_CAPACITY_LIMIT ||
        before.supervised_reserve != 1U ||
        before.peak_active_tasks < before.active_tasks ||
        before.active_tasks >= before.task_capacity ||
        before.supervised_reserve >
            before.task_capacity - before.active_tasks) return -1;

    size_t child_count = (size_t)(before.task_capacity - before.active_tasks -
                                  before.supervised_reserve);
    if (child_count == 0U ||
        child_count >= GUEST_TEST_TASK_CAPACITY_LIMIT) return -1;

    int children[GUEST_TEST_TASK_CAPACITY_LIMIT];
    size_t spawned = 0U;
    bool valid = true;
    int parent_pid = x86os_getpid();
    for (; spawned < child_count; ++spawned) {
        children[spawned] = x86os_spawn("SLEEPER.PRG");
        if (children[spawned] <= 0) {
            valid = false;
            break;
        }
    }
    if (valid) {
        int overflow_pid = x86os_spawn("SLEEPER.PRG");
        if (overflow_pid >= 0) {
            children[spawned++] = overflow_pid;
            valid = false;
        }
    }

    x86os_scheduler_stats_t exhausted = {0};
    if (valid && (x86os_scheduler_stats(&exhausted) != 0 ||
                  exhausted.task_capacity != before.task_capacity ||
                  exhausted.supervised_reserve != before.supervised_reserve ||
                  exhausted.active_tasks + exhausted.supervised_reserve !=
                      exhausted.task_capacity ||
                  exhausted.peak_active_tasks < exhausted.active_tasks ||
                  exhausted.capacity_rejections <= before.capacity_rejections))
        valid = false;

    for (size_t index = 0; valid && index < spawned; ++index) {
        x86os_process_info_t info;
        if (process_info_for_pid(children[index], &info) != 0 ||
            info.parent_pid != parent_pid ||
            (info.state != X86OS_PROCESS_READY &&
             info.state != X86OS_PROCESS_RUNNING &&
             info.state != X86OS_PROCESS_SLEEPING)) {
            valid = false;
        }
    }

    for (size_t index = 0; index < spawned; ++index) {
        int status = -1;
        int kill_result = x86os_kill(children[index]);
        int wait_result = x86os_wait(children[index], &status);
        if (kill_result != 0 || wait_result != children[index] ||
            status != 143) valid = false;
    }
    if (!valid) return -1;
    if (wait_for_expected("CHILDEX.PRG", 37) != 0) return -1;
    x86os_scheduler_stats_t reclaimed;
    return x86os_scheduler_stats(&reclaimed) == 0 &&
           reclaimed.active_tasks < exhausted.active_tasks &&
           reclaimed.peak_active_tasks >= exhausted.peak_active_tasks &&
           reclaimed.capacity_rejections >= exhausted.capacity_rejections
        ? 0 : -1;
}

static int test_storage_service(void) {
    uint8_t sector[X86OS_STORAGE_BLOCK_SIZE];
    int32_t result = -1;
    x86os_storage_handle_t handle = 0U;
    x86os_storage_submit_t request = {
        .version = X86OS_STORAGE_REQUEST_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_STORAGE_BLOCK_READ,
        .resource = 0U,
        .offset = 0U,
        .length = sizeof(sector),
        .timeout_ms = 1000U,
    };
    x86os_storage_handle_t cancel_handle = 0U;
    if (x86os_storage_cancel(0U) != -22 ||
        x86os_storage_submit(&request, 0, &cancel_handle) != 0 ||
        cancel_handle == 0U || x86os_storage_cancel(cancel_handle) != 0)
        return -1;
    int canceled_collect = x86os_storage_collect(
        cancel_handle, &result, sector);
    if (canceled_collect != -22 && canceled_collect != -125) return -1;

    if (x86os_storage_bind() != -13 ||
        x86os_storage_claim((x86os_storage_descriptor_t*)(uintptr_t)0x1000U,
                            sector) != -13 ||
        x86os_storage_block_read(0U, 0U, sector) != -13 ||
        x86os_storage_submit(
            (const x86os_storage_submit_t*)(uintptr_t)0x1000U,
            0, &handle) != -14 ||
        x86os_storage_submit(&request, 0, &handle) != 0 || handle == 0U)
        return -1;
    bool recovered = false;
    for (uint32_t attempt = 0U; attempt < 2U; ++attempt) {
        uint64_t deadline = 0U;
        if (x86os_monotonic_ms(&deadline) != 0) return -1;
        deadline += 2000U;
        for (;;) {
            int collect = x86os_storage_collect(handle, &result, sector);
            if (collect == 0) break;
            uint64_t now = 0U;
            if (collect == -22 && attempt == 0U) {
                recovered = true;
                break;
            }
            if (collect != -11 || x86os_monotonic_ms(&now) != 0 ||
                now >= deadline) return -1;
            if (x86os_sleep_ms(5U) != 0) return -1;
        }
        if (!recovered || attempt != 0U) break;
        uint64_t restart_deadline = 0U;
        if (x86os_monotonic_ms(&restart_deadline) != 0) return -1;
        restart_deadline += 2000U;
        for (;;) {
            handle = 0U;
            int submit = x86os_storage_submit(&request, 0, &handle);
            if (submit == 0 && handle != 0U) break;
            uint64_t now = 0U;
            if (submit != -112 || x86os_monotonic_ms(&now) != 0 ||
                now >= restart_deadline || x86os_sleep_ms(5U) != 0)
                return -1;
        }
    }
    if (result == -5) {
        uint64_t deadline = 0U;
        if (x86os_monotonic_ms(&deadline) != 0) return -1;
        deadline += 5000U;
        for (;;) {
            uint64_t now = 0U;
            handle = 0U;
            result = -1;
            if (x86os_storage_submit(&request, 0, &handle) != 0 ||
                handle == 0U) {
                x86os_puts("TEST_DIAG STORAGE_REINTEGRATE_SUBMIT\n");
                return -1;
            }
            for (;;) {
                int collect = x86os_storage_collect(handle, &result, sector);
                if (collect == 0) break;
                if (collect != -11 || x86os_sleep_ms(5U) != 0) {
                    x86os_puts("TEST_DIAG STORAGE_REINTEGRATE_COLLECT\n");
                    return -1;
                }
            }
            if (result == 0) break;
            if (result != -112) {
                x86os_puts(result == -5
                    ? "TEST_DIAG STORAGE_REINTEGRATE_STILL_IO\n"
                    : "TEST_DIAG STORAGE_REINTEGRATE_OTHER_RESULT\n");
                return -1;
            }
            if (x86os_monotonic_ms(&now) != 0 || now >= deadline ||
                x86os_sleep_ms(50U) != 0) {
                x86os_puts("TEST_DIAG STORAGE_REINTEGRATE_TIMEOUT\n");
                return -1;
            }
        }
        if (sector[510] != 0x55U || sector[511] != 0xAAU) return -1;
        x86os_puts("TEST_STAGE STORAGE_MEDIA_REINTEGRATED_OK\n");
        return 0;
    }
    bool signature_valid = sector[510] == 0x55U && sector[511] == 0xAAU;
    if (result != 0 || !signature_valid) return -1;
    if (recovered) x86os_puts("TEST_STAGE STORAGE_RESTART_OK\n");
    return 0;
}

static int read_hotplug_file(void) {
    static const char expected[] = "REIST-HOTPLUG\n";
    char buffer[sizeof(expected)];
    int descriptor = x86os_open("/mnt/fdd0/HOTPLUG.TXT");
    if (descriptor < 0) return -1;
    int amount = x86os_read(descriptor, buffer, sizeof(expected) - 1U);
    int closed = x86os_close(descriptor);
    if (amount != (int)(sizeof(expected) - 1U) || closed != 0) return -1;
    for (size_t index = 0U; index < sizeof(expected) - 1U; ++index) {
        if (buffer[index] != expected[index]) return -1;
    }
    return 0;
}

static int fdd_hotplug_main(void) {
    if (read_hotplug_file() != 0) {
        x86os_puts("TEST_FAIL FDD_HOTPLUG_INITIAL_READ\n");
        return 1;
    }
    x86os_puts("REIST_FDD HOTPLUG_ARMED\n");
    if (x86os_sleep_ms(500U) != 0 || read_hotplug_file() == 0) {
        x86os_puts("TEST_FAIL FDD_DISCONNECT_NOT_DETECTED\n");
        return 2;
    }
    x86os_puts("REIST_FDD DISCONNECT_DETECTED\n");

    uint64_t deadline = 0U;
    if (x86os_monotonic_ms(&deadline) != 0) return 3;
    deadline += 15000U;
    for (;;) {
        if (read_hotplug_file() == 0) break;
        uint64_t now = 0U;
        if (x86os_monotonic_ms(&now) != 0 || now >= deadline ||
            x86os_sleep_ms(250U) != 0) {
            x86os_puts("TEST_FAIL FDD_REINTEGRATION_TIMEOUT\n");
            return 4;
        }
    }
    x86os_puts("TEST_STAGE FDD_HOTPLUG_REINTEGRATED_OK\n");
    x86os_puts("TEST_OK\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && text_equal(argv[1], "FDD_HOTPLUG"))
        return fdd_hotplug_main();
    if (argc == 3 && text_equal(argv[1], "IPC_ECHO"))
        return ipc_child_main(argv[1], argv[2]);
    if (argc == 3 && text_equal(argv[1], "IPC_WAIT_CLOSE"))
        return ipc_child_main(argv[1], argv[2]);
    if (argc == 3 && text_equal(argv[1], "IPC_EXIT"))
        return ipc_child_main(argv[1], argv[2]);
    if (argc == 2 && text_equal(argv[1], "VFS_ADOPT"))
        return vfs_delegation_child_main();
    if (argc == 2 && text_equal(argv[1], "VFS_EXPIRE"))
        return vfs_delegation_expiry_child_main();

    x86os_puts("GUEST_TEST_BEGIN\n");

    if (test_standard_descriptors() != 0) {
        x86os_puts("TEST_FAIL STANDARD_DESCRIPTORS\n");
        return 10;
    }
    x86os_puts("TEST_STAGE STANDARD_DESCRIPTORS_OK\n");

    if (test_open_flags() != 0) {
        x86os_puts("TEST_FAIL OPEN_FLAGS\n");
        return 11;
    }
    x86os_puts("TEST_STAGE OPEN_FLAGS_OK\n");

    if (test_descriptor_seek_fstat() != 0) {
        x86os_puts("TEST_FAIL DESCRIPTOR_SEEK_FSTAT\n");
        return 12;
    }
    x86os_puts("TEST_STAGE DESCRIPTOR_SEEK_FSTAT_OK\n");

    if (test_ftruncate() != 0) {
        x86os_puts("TEST_FAIL FTRUNCATE\n");
        return 13;
    }
    x86os_puts("TEST_STAGE FTRUNCATE_OK\n");

    if (test_fat_timestamps() != 0) {
        x86os_puts("TEST_FAIL FAT_TIMESTAMPS\n");
        return 14;
    }
    x86os_puts("TEST_STAGE FAT_TIMESTAMPS_OK\n");

    if (test_vfat_utf8() != 0) {
        x86os_puts("TEST_FAIL VFAT_UTF8\n");
        return 15;
    }
    x86os_puts("TEST_STAGE VFAT_UTF8_OK\n");

    if (test_unicode_raster() != 0) {
        x86os_puts("TEST_FAIL UNICODE_RASTER\n");
        return 16;
    }
    x86os_puts("TEST_STAGE UNICODE_RASTER_OK\n");

    if (test_open_namespace_locks() != 0) {
        x86os_puts("TEST_FAIL OPEN_NAMESPACE_LOCKS\n");
        return 1;
    }
    x86os_puts("TEST_STAGE OPEN_NAMESPACE_LOCKS_OK\n");

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
    if (test_vfs_readonly_walkers() != 0) {
        x86os_puts("TEST_FAIL VFS_READONLY_WALKERS\n");
        return 18;
    }
    x86os_puts("TEST_STAGE VFS_READONLY_WALKERS_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_SHADOW_STAT_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_FAT32_PARSER_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_FAT_STAT_AUTHORITY_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_FS_STAT_AUTHORITY_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_STAT_CLIENT_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_READ_CLIENT_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_READ_SESSION_OK\n");
    x86os_puts("TEST_STAGE STORAGE_CLAIM_IDENTITY_OK\n");
    x86os_puts("TEST_STAGE STORAGE_VFS_OBJECT_HANDLE_OK\n");

    if (test_vfs_object_delegation() != 0) {
        x86os_puts("TEST_FAIL STORAGE_VFS_DELEGATION\n");
        return 9;
    }
    x86os_puts("TEST_STAGE STORAGE_VFS_DELEGATION_OK\n");

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

    if (test_storage_service() != 0) {
        x86os_puts("TEST_FAIL STORAGE_SERVICE\n");
        return 7;
    }
    x86os_puts("TEST_STAGE STORAGE_SERVICE_OK\n");
    x86os_puts("TEST_STAGE STORAGE_REQUEST_CANCEL_OK\n");

    if (test_task_capacity_and_parenting() != 0) {
        x86os_puts("TEST_FAIL TASK_CAPACITY\n");
        return 8;
    }
    x86os_puts("TEST_STAGE TASK_CAPACITY_OK\n");
    x86os_puts("TEST_STAGE REIST_PROGRESS_OK\n");

    if (wait_for_expected("FAULTDE.PRG", 128) != 0 ||
        wait_for_expected("FAULTUD.PRG", 134) != 0 ||
        wait_for_expected("FAULTPF.PRG", 142) != 0 ||
        wait_for_expected("FAULTSTK.PRG", 142) != 0) {
        x86os_puts("TEST_FAIL EXCEPTIONS\n");
        return 9;
    }
    x86os_puts("TEST_STAGE EXCEPTIONS_OK\n");
    x86os_puts("TEST_OK\n");
    return 0;
}
