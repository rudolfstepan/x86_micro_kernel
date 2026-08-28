/**
 * @file include/kernel/kernel_log.h
 * @brief Begrenztes Kernel-Ereignisprotokoll ohne VFS-Abhängigkeit.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Einträge fester Größe werden in kapazitätsbegrenztem Speicher abgelegt.
 * Safety: Überlauf ist erkennbar; Logging blockiert keine Sicherheitsübergänge.
 */
#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_LOG_CAPACITY 32768U
#define KERNEL_LOG_READ_FROM_OLDEST (1U << 0U)

typedef enum {
    KLOG_TRACE = 0,
    KLOG_DEBUG,
    KLOG_INFO,
    KLOG_WARN,
    KLOG_ERROR
} kernel_log_level_t;

typedef struct {
    uint32_t oldest_cursor;
    uint32_t snapshot_head;
    uint32_t next_cursor;
    uint32_t copied;
    uint32_t dropped;
    uint32_t overwritten;
} kernel_log_read_result_t;

void kernel_log_set_minimum(kernel_log_level_t level);
kernel_log_level_t kernel_log_minimum(void);
void kernel_log_capture_char(char value);
int kernel_log_read(uint32_t cursor, uint32_t flags, char *buffer,
                    size_t capacity, kernel_log_read_result_t *result);
void klog(kernel_log_level_t level, const char *component,
          const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#endif
