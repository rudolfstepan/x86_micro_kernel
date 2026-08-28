/**
 * @file kernel/init/kernel_log_buffer.c
 * @brief Fester, SMP-sicherer Ring fuer Ring-0-Konsolenausgaben.
 *
 * Layer: Ring-0 diagnostics mechanism.
 * Contract: Producer reservieren eine monotone Zeichensequenz; Reader erhalten
 *           nur vollstaendig publizierte, zusammenhaengende Zeichen.
 * Safety: Keine Allokation, kein VFS, kein Warten und keine Formatierung.
 */
#include "include/kernel/kernel_log.h"

#include <limits.h>

typedef struct {
    uint32_t sequence;
    uint8_t value;
    uint8_t reserved[3];
} kernel_log_slot_t;

static kernel_log_slot_t slots[KERNEL_LOG_CAPACITY];
static uint32_t reserved_head;
static uint32_t producer_dropped;

static void increment_saturating(uint32_t *value) {
    uint32_t observed = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (observed != UINT32_MAX &&
           !__atomic_compare_exchange_n(value, &observed, observed + 1U,
                                        0, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
}

void kernel_log_capture_char(char value) {
    uint32_t sequence = __atomic_load_n(&reserved_head, __ATOMIC_RELAXED);
    for (;;) {
        if (sequence == UINT32_MAX) {
            increment_saturating(&producer_dropped);
            return;
        }
        uint32_t next = sequence + 1U;
        if (__atomic_compare_exchange_n(&reserved_head, &sequence, next, 0,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED))
            break;
    }

    kernel_log_slot_t *slot = &slots[sequence % KERNEL_LOG_CAPACITY];
    __atomic_store_n(&slot->value, (uint8_t)value, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->sequence, sequence + 1U, __ATOMIC_RELEASE);
}

int kernel_log_read(uint32_t cursor, uint32_t flags, char *buffer,
                    size_t capacity, kernel_log_read_result_t *result) {
    if (buffer == NULL || result == NULL || capacity == 0U ||
        (flags & ~KERNEL_LOG_READ_FROM_OLDEST) != 0U)
        return -22;

    const uint32_t head = __atomic_load_n(&reserved_head, __ATOMIC_ACQUIRE);
    const uint32_t oldest = head > KERNEL_LOG_CAPACITY
        ? head - KERNEL_LOG_CAPACITY : 0U;
    uint32_t dropped = __atomic_load_n(&producer_dropped, __ATOMIC_RELAXED);
    if ((flags & KERNEL_LOG_READ_FROM_OLDEST) != 0U) {
        cursor = oldest;
    } else if (cursor < oldest) {
        uint32_t stale = oldest - cursor;
        dropped = UINT32_MAX - dropped < stale
            ? UINT32_MAX : dropped + stale;
        cursor = oldest;
    } else if (cursor > head) {
        return -22;
    }

    result->oldest_cursor = oldest;
    result->snapshot_head = head;
    result->next_cursor = cursor;
    result->copied = 0U;
    result->dropped = dropped;
    result->overwritten = oldest;

    while (cursor < head && result->copied < capacity) {
        kernel_log_slot_t *slot = &slots[cursor % KERNEL_LOG_CAPACITY];
        const uint32_t expected = cursor + 1U;
        if (__atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE) != expected)
            break;
        uint8_t value = __atomic_load_n(&slot->value, __ATOMIC_RELAXED);
        if (__atomic_load_n(&slot->sequence, __ATOMIC_ACQUIRE) != expected)
            break;
        buffer[result->copied++] = (char)value;
        cursor++;
    }
    result->next_cursor = cursor;
    return 0;
}
