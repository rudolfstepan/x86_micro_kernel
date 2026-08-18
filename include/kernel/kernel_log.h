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

typedef enum {
    KLOG_TRACE = 0,
    KLOG_DEBUG,
    KLOG_INFO,
    KLOG_WARN,
    KLOG_ERROR
} kernel_log_level_t;

void kernel_log_set_minimum(kernel_log_level_t level);
kernel_log_level_t kernel_log_minimum(void);
void klog(kernel_log_level_t level, const char *component,
          const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#endif
