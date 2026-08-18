/**
 * @file kernel/init/kernel_log.c
 * @brief Speichert feste Diagnoseereignisse in einem begrenzten Ringpuffer.
 *
 * Layer: Ring-0 diagnostics.
 * Contract: Producer publizieren vollständige Einträge; Reader erhalten Snapshots.
 * Safety: Überlauf blockiert keinen kritischen Pfad.
 */
#include "include/kernel/kernel_log.h"

#include <stdarg.h>

#include "include/kernel/panic.h"
#include "kernel/sched/scheduler.h"
#include "lib/libc/stdio.h"

static kernel_log_level_t minimum_level = KLOG_INFO;

static const char *const level_names[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR"
};

void kernel_log_set_minimum(kernel_log_level_t level) {
    KASSERT_NOT_IRQ();
    KASSERT(level >= KLOG_TRACE && level <= KLOG_ERROR);
    minimum_level = level;
}

kernel_log_level_t kernel_log_minimum(void) {
    return minimum_level;
}

void klog(kernel_log_level_t level, const char *component,
          const char *format, ...) {
    KASSERT_NOT_IRQ();
    KASSERT(level >= KLOG_TRACE && level <= KLOG_ERROR);
    if (level < minimum_level) return;

    scheduler_preempt_disable();
    printf("[%s][%s] ", level_names[level],
           component != NULL ? component : "kernel");
    va_list arguments;
    va_start(arguments, format);
    (void)vprintf(format, arguments);
    va_end(arguments);
    printf("\n");
    scheduler_preempt_enable();
}
