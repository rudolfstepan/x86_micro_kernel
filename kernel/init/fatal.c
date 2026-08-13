#include "include/kernel/fatal.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/char/io.h"
#include "drivers/char/serial.h"
#include "include/kernel/watchdog.h"
#include "include/kernel/output_fence.h"
#include <stdbool.h>

#define COM1_LINE_STATUS (SERIAL_COM1 + 5U)
#define COM1_TX_READY 0x20U
#define EMERGENCY_SERIAL_POLL_BUDGET 65536U
#define CMOS_INDEX_PORT 0x70U
#define CMOS_DATA_PORT 0x71U
#define CMOS_CRASH_RECORD_BASE 0x38U

_Static_assert(sizeof(fatal_crash_record_t) == 20U,
               "CMOS crash-record layout must remain fixed");
static fatal_crash_record_t recovered_record;

static volatile fatal_crash_record_t *persistent_record(void) {
    return (volatile fatal_crash_record_t *)(uintptr_t)
        FATAL_CRASH_RECORD_ADDRESS;
}

static uint8_t cmos_read_byte(uint8_t index) {
    outb(CMOS_INDEX_PORT, (uint8_t)(0x80U | index));
    uint8_t value = inb(CMOS_DATA_PORT);
    outb(CMOS_INDEX_PORT, 0U);
    return value;
}

static void cmos_write_byte(uint8_t index, uint8_t value) {
    outb(CMOS_INDEX_PORT, (uint8_t)(0x80U | index));
    outb(CMOS_DATA_PORT, value);
    outb(CMOS_INDEX_PORT, 0U);
}

static fatal_crash_record_t cmos_read_record(void) {
    fatal_crash_record_t record;
    uint8_t *bytes = (uint8_t *)&record;
    for (uint32_t index = 0; index < sizeof(record); ++index) {
        bytes[index] = cmos_read_byte((uint8_t)(CMOS_CRASH_RECORD_BASE + index));
    }
    return record;
}

static void cmos_write_record(const fatal_crash_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    for (uint32_t index = 0; index < sizeof(record->magic); ++index) {
        cmos_write_byte((uint8_t)(CMOS_CRASH_RECORD_BASE + index), 0U);
    }
    for (uint32_t index = sizeof(record->magic); index < sizeof(*record); ++index) {
        cmos_write_byte((uint8_t)(CMOS_CRASH_RECORD_BASE + index), bytes[index]);
    }
    for (uint32_t index = 0; index < sizeof(record->magic); ++index) {
        cmos_write_byte((uint8_t)(CMOS_CRASH_RECORD_BASE + index), bytes[index]);
    }
}

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
    return &recovered_record;
}

static bool persistent_record_valid(const volatile fatal_crash_record_t *record) {
    fatal_crash_record_t copy = {
        record->magic, record->version, record->reason, record->sequence,
        record->checksum
    };
    return copy.magic == FATAL_CRASH_RECORD_MAGIC &&
           copy.version == FATAL_CRASH_RECORD_VERSION &&
           copy.checksum == crash_checksum(&copy);
}

void fatal_boot_recover_record(void) {
    volatile fatal_crash_record_t *record = persistent_record();
    fatal_crash_record_t nvram_record = cmos_read_record();
    const volatile fatal_crash_record_t *source =
        persistent_record_valid((const volatile fatal_crash_record_t *)&nvram_record)
            ? (const volatile fatal_crash_record_t *)&nvram_record : record;
    if (!persistent_record_valid(source)) return;
    recovered_record.magic = source->magic;
    recovered_record.version = source->version;
    recovered_record.reason = source->reason;
    recovered_record.sequence = source->sequence;
    recovered_record.checksum = source->checksum;
    /* Consume atomically enough for a single-core boot: an interrupted clear
     * can only cause the same valid record to be reported once more. */
    record->magic = 0;
    for (uint32_t index = 0; index < sizeof(record->magic); ++index) {
        cmos_write_byte((uint8_t)(CMOS_CRASH_RECORD_BASE + index), 0U);
    }
    emergency_serial_text("REIST_RECOVERY PREVIOUS_FATAL\n");
}

static void __attribute__((noreturn)) controlled_reset(void) {
    /* Request a platform reset through the legacy controller.  Every poll is
     * bounded because a failed controller must not trap the fatal path. */
    for (uint32_t remaining = EMERGENCY_SERIAL_POLL_BUDGET;
         remaining != 0; --remaining) {
        if ((inb(0x64U) & 0x02U) == 0) {
            outb(0x64U, 0xFEU);
            break;
        }
    }

    /* If the reset request is ignored, force a CPU reset through a deliberate
     * triple fault.  A product target must replace this fallback with an
     * independently powered watchdog and output fencing. */
    struct __attribute__((packed)) {
        uint16_t limit;
        uint32_t base;
    } null_idt = {0, 0};
    __asm__ __volatile__("lidt %0; int $3" : : "m"(null_idt) : "memory");
    cpu_halt_forever();
    __builtin_unreachable();
}

void __attribute__((noreturn)) fatal_emergency_handoff(uint32_t reason) {
    irq_disable();
    /* Revoke hazardous software/hardware outputs before diagnosis or reset. */
    output_fence_all();
    volatile fatal_crash_record_t *destination = persistent_record();
    uint32_t sequence = persistent_record_valid(destination)
                            ? destination->sequence + 1U : 1U;
    fatal_crash_record_t record = {
        .magic = FATAL_CRASH_RECORD_MAGIC,
        .version = FATAL_CRASH_RECORD_VERSION,
        .reason = reason,
        .sequence = sequence,
        .checksum = 0,
    };
    record.checksum = crash_checksum(&record);
    /* Magic is the commit word and is published last. */
    destination->magic = 0;
    destination->version = record.version;
    destination->reason = record.reason;
    destination->sequence = record.sequence;
    destination->checksum = record.checksum;
    __asm__ __volatile__("" : : : "memory");
    destination->magic = record.magic;
    cmos_write_record(&record);

    emergency_serial_text(reason == FATAL_REASON_DOUBLE_FAULT
                              ? "REIST_FATAL DOUBLE_FAULT RESET\n"
                              : "REIST_FATAL KERNEL RESET\n");
    if (watchdog_fatal_handoff()) {
        /* The independent device owns the primary reset. This bounded wait
         * gives it time to act; controlled_reset remains the fail-closed
         * fallback if the device or platform integration is defective. */
        for (volatile uint32_t remaining = 100000000U;
             remaining != 0; --remaining) {
            __asm__ __volatile__("pause");
        }
    }
    controlled_reset();
    __builtin_unreachable();
}

void __attribute__((noreturn)) double_fault_emergency_entry(void) {
    fatal_emergency_handoff(FATAL_REASON_DOUBLE_FAULT);
}

#ifdef REIST_FAULT_INJECTION
void __attribute__((noreturn)) fatal_test_trigger_double_fault(void) {
    /* Vector 8 is a task gate. A ring-0 software interrupt exercises the same
     * dedicated TSS/emergency-stack entry without first corrupting memory. */
    __asm__ __volatile__("int $8");
    cpu_halt_forever();
    __builtin_unreachable();
}
#endif
