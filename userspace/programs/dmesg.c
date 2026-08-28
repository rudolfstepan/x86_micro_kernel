/**
 * @file userspace/programs/dmesg.c
 * @brief Liest den begrenzten Ring-0-Konsolenlog mit einem 80x25-Pager.
 */
#include "x86os.h"

#include <stddef.h>
#include <stdint.h>

#define DMESG_PAGE_LINES 22U
#define DMESG_MAX_READS 160U
#define DMESG_EMPTY_RETRIES 8U

static char chunk[X86OS_KERNEL_LOG_READ_MAX];

static int text_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return 0;
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) return 0;
    }
    return *left == *right;
}

static int pager_continue(unsigned int *lines) {
    x86os_puts("--More-- [Space/Enter/Q]");
    for (;;) {
        int key = x86os_getchar();
        if (key == 'q' || key == 'Q') {
            x86os_putchar('\n');
            return 0;
        }
        if (key == ' ') {
            x86os_clear();
            *lines = 0U;
            return 1;
        }
        if (key == '\r' || key == '\n') {
            x86os_putchar('\n');
            *lines = DMESG_PAGE_LINES - 1U;
            return 1;
        }
    }
}

static int emit_log(const char *data, uint32_t length, int pager,
                    unsigned int *lines) {
    for (uint32_t index = 0U; index < length; ++index) {
        x86os_putchar(data[index]);
        if (data[index] != '\n') continue;
        if (*lines != UINT32_MAX) (*lines)++;
        if (pager && *lines >= DMESG_PAGE_LINES && !pager_continue(lines))
            return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    int pager = 1;
    if (argc == 2 && text_equal(argv[1], "--no-pager")) {
        pager = 0;
    } else if (argc == 2 &&
               (text_equal(argv[1], "--help") ||
                text_equal(argv[1], "-h"))) {
        x86os_puts("Usage: dmesg [--no-pager]\n");
        return 0;
    } else if (argc != 1) {
        x86os_puts("dmesg: invalid arguments\n");
        return 2;
    }

    x86os_kernel_log_read_t request = {
        .flags = X86OS_KERNEL_LOG_READ_FROM_OLDEST,
        .cursor = 0U,
        .buffer_address = (uint32_t)(uintptr_t)chunk,
        .buffer_capacity = sizeof(chunk),
    };
    uint32_t target = 0U;
    uint32_t empty_retries = 0U;
    unsigned int lines = 0U;
    int first = 1;

    for (uint32_t read_count = 0U; read_count < DMESG_MAX_READS;
         ++read_count) {
        int status = x86os_kernel_log_read(&request);
        if (status != 0) {
            x86os_puts("dmesg: kernel log read failed code=");
            x86os_print_number(status);
            x86os_putchar('\n');
            return 1;
        }
        if (first) {
            target = request.snapshot_head;
            if (request.overwritten != 0U)
                x86os_puts("dmesg: older records were overwritten\n");
            if (request.dropped != 0U)
                x86os_puts("dmesg: producer or cursor records were dropped\n");
            first = 0;
        }

        uint32_t chunk_start = request.next_cursor - request.copied;
        uint32_t amount = request.copied;
        if (chunk_start >= target) amount = 0U;
        else if (amount > target - chunk_start) amount = target - chunk_start;
        if (amount != 0U) {
            empty_retries = 0U;
            if (!emit_log(chunk, amount, pager, &lines)) return 0;
        } else if (request.next_cursor < target) {
            if (++empty_retries > DMESG_EMPTY_RETRIES) {
                x86os_puts("dmesg: log publication did not stabilize\n");
                return 1;
            }
            (void)x86os_sleep_ms(10U);
        }

        if (request.next_cursor >= target) return 0;
        request.flags = 0U;
        request.cursor = request.next_cursor;
    }
    x86os_puts("dmesg: bounded read limit reached\n");
    return 1;
}
