/** @file test_kernel_log_host.c @brief Bounded kernel-log ring behavior. */
#include "include/kernel/kernel_log.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void append_text(const char *text) {
    while (*text != '\0') kernel_log_capture_char(*text++);
}

int main(void) {
    char output[16] = {0};
    kernel_log_read_result_t result;

    assert(kernel_log_read(0U, KERNEL_LOG_READ_FROM_OLDEST, output,
                           sizeof(output), &result) == 0);
    assert(result.copied == 0U && result.oldest_cursor == 0U &&
           result.snapshot_head == 0U && result.next_cursor == 0U);

    append_text("abc\n");
    assert(kernel_log_read(0U, KERNEL_LOG_READ_FROM_OLDEST, output, 2U,
                           &result) == 0);
    assert(result.copied == 2U && result.next_cursor == 2U &&
           result.snapshot_head == 4U && memcmp(output, "ab", 2U) == 0);
    assert(kernel_log_read(result.next_cursor, 0U, output, sizeof(output),
                           &result) == 0);
    assert(result.copied == 2U && result.next_cursor == 4U &&
           memcmp(output, "c\n", 2U) == 0);

    for (uint32_t index = 0U; index < KERNEL_LOG_CAPACITY + 5U; ++index)
        kernel_log_capture_char((char)('A' + index % 26U));
    assert(kernel_log_read(0U, 0U, output, sizeof(output), &result) == 0);
    assert(result.oldest_cursor == 9U);
    assert(result.dropped == 9U);
    assert(result.next_cursor == result.oldest_cursor + result.copied);
    assert(result.copied == sizeof(output));

    assert(kernel_log_read(0U, KERNEL_LOG_READ_FROM_OLDEST, output, 0U,
                           &result) == -22);
    assert(kernel_log_read(0U, KERNEL_LOG_READ_FROM_OLDEST, NULL,
                           sizeof(output), &result) == -22);
    assert(kernel_log_read(0U, KERNEL_LOG_READ_FROM_OLDEST, output,
                           sizeof(output), NULL) == -22);
    return 0;
}
