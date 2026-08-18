/**
 * @file kernel/init/output_fence.c
 * @brief Koordiniert Ausgabesperren und deren Rückleseprüfung.
 *
 * Layer: Ring-0 safety control.
 * Contract: Backends werden stabil angewendet und anschließend verifiziert.
 * Safety: Fehlende Bestätigung lässt den globalen Fence unbestätigt.
 */
#include "include/kernel/output_fence.h"

#include <stddef.h>

/* Fatal-path primitive: fixed storage, no allocation, locks, I/O or logging.
 * Handlers must be bounded, idempotent and safe with interrupts disabled. */
static output_fence_handler_t handlers[OUTPUT_FENCE_MAX_HANDLERS];
static volatile uint32_t handler_count;
static volatile uint32_t fence_active;

void output_fence_init(void) {
    handler_count = 0;
    fence_active = 0;
    for (uint32_t i = 0; i < OUTPUT_FENCE_MAX_HANDLERS; ++i) handlers[i] = NULL;
}

bool output_fence_register(output_fence_handler_t handler) {
    if (handler == NULL || fence_active != 0) return false;
    uint32_t count = handler_count;
    for (uint32_t i = 0; i < count; ++i) {
        if (handlers[i] == handler) return true;
    }
    if (count >= OUTPUT_FENCE_MAX_HANDLERS) return false;
    handlers[count] = handler;
    __asm__ __volatile__("" : : : "memory");
    handler_count = count + 1U;
    return true;
}

void output_fence_all(void) {
    if (__sync_lock_test_and_set(&fence_active, 1U) != 0) return;
    uint32_t count = handler_count;
    for (uint32_t i = 0; i < count; ++i) handlers[i]();
}

bool output_fence_is_active(void) {
    return fence_active != 0;
}

uint32_t output_fence_handler_count(void) {
    return handler_count;
}
