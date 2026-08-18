/**
 * @file include/kernel/fatal.h
 * @brief Fatalpfad für begrenzte Diagnose, Output-Fencing und kontrollierten Neustart.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Aufruf erfolgt für nicht lokal beherrschbare Kernfehler und kehrt regulär nicht zurück.
 * Safety: Der Pfad vermeidet Heap, VFS und ungebundene Arbeit.
 */
#ifndef KERNEL_FATAL_H
#define KERNEL_FATAL_H

#include <stdint.h>

#define FATAL_CRASH_RECORD_MAGIC 0x52454953U /* "REIS" */
#define FATAL_CRASH_RECORD_VERSION 1U
#define FATAL_CRASH_RECORD_ADDRESS 0x00030000U
#define FATAL_CRASH_RECORD_REGION_SIZE 4096U

#define FATAL_REASON_DOUBLE_FAULT 8U
#define FATAL_REASON_KERNEL_PANIC 0x100U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t reason;
    uint32_t sequence;
    uint32_t checksum;
} fatal_crash_record_t;

const volatile fatal_crash_record_t *fatal_last_crash_record(void);
void fatal_boot_recover_record(void);
void fatal_emergency_handoff(uint32_t reason) __attribute__((noreturn));
void double_fault_emergency_entry(void) __attribute__((noreturn));
#ifdef REIST_FAULT_INJECTION
void fatal_test_trigger_double_fault(void) __attribute__((noreturn));
#endif

#endif
