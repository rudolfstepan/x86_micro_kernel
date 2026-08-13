#ifndef KERNEL_FATAL_H
#define KERNEL_FATAL_H

#include <stdint.h>

#define FATAL_CRASH_RECORD_MAGIC 0x52454953U /* "REIS" */
#define FATAL_CRASH_RECORD_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t reason;
    uint32_t sequence;
    uint32_t checksum;
} fatal_crash_record_t;

const volatile fatal_crash_record_t *fatal_last_crash_record(void);
void double_fault_emergency_entry(void) __attribute__((noreturn));

#endif
