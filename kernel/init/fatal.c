#include "include/kernel/fatal.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/char/io.h"
#include "drivers/char/serial.h"

#define COM1_LINE_STATUS (SERIAL_COM1 + 5U)
#define COM1_TX_READY 0x20U
#define EMERGENCY_SERIAL_POLL_BUDGET 65536U
#define FATAL_REASON_DOUBLE_FAULT 8U

static volatile fatal_crash_record_t crash_record;
static uint32_t crash_sequence;

static uint32_t crash_checksum(const fatal_crash_record_t *record) {
    return record->magic ^ record->version ^ record->reason ^ record->sequence ^
           0xFFFFFFFFU;
}

static void emergency_serial_char(char value) {
    for (uint32_t remaining = EMERGENCY_SERIAL_POLL_BUDGET;
         remaining != 0; --remaining) {
        if ((inb(COM1_LINE_STATUS) & COM1_TX_READY) != 0) {
            outb(SERIAL_COM1, (uint8_t)value);
            return;
        }
    }
}

static void emergency_serial_text(const char *text) {
    while (text != 0 && *text != '\0') emergency_serial_char(*text++);
}

const volatile fatal_crash_record_t *fatal_last_crash_record(void) {
    return &crash_record;
}

void __attribute__((noreturn)) double_fault_emergency_entry(void) {
    irq_disable();
    fatal_crash_record_t record = {
        .magic = FATAL_CRASH_RECORD_MAGIC,
        .version = FATAL_CRASH_RECORD_VERSION,
        .reason = FATAL_REASON_DOUBLE_FAULT,
        .sequence = ++crash_sequence,
        .checksum = 0,
    };
    record.checksum = crash_checksum(&record);
    crash_record = record;

    emergency_serial_text("REIST_FATAL DOUBLE_FAULT\n");

    /* S0.2 v1 contains the independent emergency entry and bounded record.
     * The external watchdog/fencing handoff is the next REIST increment. */
    cpu_halt_forever();
    __builtin_unreachable();
}
