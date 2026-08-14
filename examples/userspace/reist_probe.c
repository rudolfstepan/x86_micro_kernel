#include <stdint.h>
#include "x86os.h"

static int text_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int report_startup(x86os_ipc_handle_t *endpoint) {
    if (x86os_ipc_create(endpoint) != 0 || *endpoint == 0U) return -1;
    if (x86os_reist_report(X86OS_REIST_REPORT_SELF_TEST, *endpoint) != 0)
        return -1;
    return x86os_reist_report(X86OS_REIST_REPORT_PROGRESS, 1U);
}

static void message_init(x86os_ipc_message_t *message, const char *text) {
    message->version = X86OS_IPC_MESSAGE_VERSION;
    message->struct_size = sizeof(*message);
    message->length = 0U;
    while (text[message->length] != '\0' &&
           message->length < X86OS_IPC_MAX_MESSAGE_SIZE) {
        message->payload[message->length] = (uint8_t)text[message->length];
        ++message->length;
    }
}

static int message_is(const x86os_ipc_message_t *message, const char *text) {
    uint32_t length = 0U;
    while (text[length] != '\0') ++length;
    if (message->length != length) return 0;
    for (uint32_t index = 0; index < length; ++index) {
        if (message->payload[index] != (uint8_t)text[index]) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    x86os_ipc_handle_t endpoint = 0U;
    if (report_startup(&endpoint) != 0) return 3;

    if (text_equal(argv[1], "crash")) {
        (void)x86os_sleep_ms(30U);
        __asm__ volatile("ud2");
        return 4;
    }
    if (text_equal(argv[1], "hang")) {
        for (;;) (void)x86os_sleep_ms(1000U);
    }
    if (text_equal(argv[1], "invalid")) {
        (void)x86os_sleep_ms(30U);
        (void)x86os_reist_report(X86OS_REIST_REPORT_INVALID, 0U);
        for (;;) (void)x86os_sleep_ms(1000U);
    }
    if (!text_equal(argv[1], "healthy")) return 5;

    uint32_t sequence = 2U;
    for (;;) {
        x86os_ipc_message_t request;
        message_init(&request, "");
        int receive = x86os_ipc_receive_timeout(endpoint, &request, 40U);
        if (receive == 0) {
            x86os_ipc_message_t response;
            message_init(&response, message_is(&request, "DIAG")
                         ? "REIST_DIAG_OK" : "REIST_DIAG_INVALID");
            if (x86os_ipc_send_timeout(endpoint, &response, 100U) != 0)
                return 7;
        } else if (receive != -110 && receive != -11) {
            return 8;
        }
        if (x86os_reist_report(X86OS_REIST_REPORT_PROGRESS, sequence++) != 0)
            return 6;
    }
}
