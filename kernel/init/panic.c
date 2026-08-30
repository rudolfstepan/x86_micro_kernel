/**
 * @file panic.c
 * @brief Kernel panic handler implementation
 */

#include "include/kernel/panic.h"
#include "arch/x86/include/interrupt.h"
#include "drivers/char/serial.h"

#include <stddef.h>
#include <stdint.h>

#define GNU_BUILD_ID_NAME_SIZE 4U
#define GNU_BUILD_ID_SHA1_SIZE 20U
#define GNU_BUILD_ID_NOTE_TYPE 3U
#define EXCEPTION_KERNEL_FRAME_BYTES 20U
#define PANIC_CONTEXT_TEXT_CAPACITY 32U
#define PANIC_CONTEXT_VERSION 1U
#define PANIC_OUTPUT_TEXT_LIMIT 160U

extern const uint8_t _kernel_build_id_note_start[];
extern const uint8_t _kernel_build_id_note_end[];

/* Exactly one CPU may enter diagnostic output. Recursive or concurrent panic
 * entry observes the atomic claim and halts before touching display, locks or
 * shared diagnostic state. */
static volatile uint32_t panic_in_progress;
static char build_id_text[GNU_BUILD_ID_SHA1_SIZE * 2U + 1U];
static int build_id_initialized = 0;

typedef struct {
    uint32_t version;
    uint32_t sequence;
    char phase[PANIC_CONTEXT_TEXT_CAPACITY];
    char component[PANIC_CONTEXT_TEXT_CAPACITY];
    char operation[PANIC_CONTEXT_TEXT_CAPACITY];
    char subject[PANIC_CONTEXT_TEXT_CAPACITY];
    int32_t result;
    uint32_t detail0;
    uint32_t detail1;
    uint32_t has_result;
    uint32_t checksum;
} panic_context_record_t;

static panic_context_record_t panic_context_slots[2];
static volatile uint32_t panic_context_active;
static uint32_t panic_context_sequence;

static void context_copy_text(char output[PANIC_CONTEXT_TEXT_CAPACITY],
                              const char *input) {
    uint32_t index = 0U;
    if (input != NULL) {
        while (index + 1U < PANIC_CONTEXT_TEXT_CAPACITY && input[index] != '\0') {
            output[index] = input[index];
            ++index;
        }
    }
    output[index] = '\0';
    for (++index; index < PANIC_CONTEXT_TEXT_CAPACITY; ++index)
        output[index] = '\0';
}

static uint32_t context_checksum(const panic_context_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < offsetof(panic_context_record_t, checksum);
         ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static bool context_valid(const panic_context_record_t *record) {
    return record != NULL && record->version == PANIC_CONTEXT_VERSION &&
           record->sequence != 0U &&
           record->checksum == context_checksum(record);
}

static void context_publish(panic_context_record_t *record) {
    uint32_t next = (panic_context_active ^ 1U) & 1U;
    record->version = PANIC_CONTEXT_VERSION;
    ++panic_context_sequence;
    if (panic_context_sequence == 0U) ++panic_context_sequence;
    record->sequence = panic_context_sequence;
    record->checksum = context_checksum(record);
    panic_context_slots[next] = *record;
    __asm__ __volatile__("" ::: "memory");
    panic_context_active = next;
}

void panic_context_set(const char *phase, const char *component,
                       const char *operation, const char *subject) {
    panic_context_record_t record = {0};
    context_copy_text(record.phase, phase);
    context_copy_text(record.component, component);
    context_copy_text(record.operation, operation);
    context_copy_text(record.subject, subject);
    uint32_t flags = irq_save();
    context_publish(&record);
    irq_restore(flags);
}

void panic_context_set_result(int32_t result, uint32_t detail0,
                              uint32_t detail1) {
    uint32_t flags = irq_save();
    panic_context_record_t record =
        panic_context_slots[panic_context_active & 1U];
    if (!context_valid(&record)) record = (panic_context_record_t){0};
    record.result = result;
    record.detail0 = detail0;
    record.detail1 = detail1;
    record.has_result = 1U;
    context_publish(&record);
    irq_restore(flags);
}

static bool panic_context_snapshot(panic_context_record_t *output) {
    if (output == NULL) return false;
    uint32_t selected = panic_context_active & 1U;
    *output = panic_context_slots[selected];
    if (context_valid(output)) return true;
    *output = panic_context_slots[selected ^ 1U];
    return context_valid(output);
}

/**
 * Halt the CPU forever
 */
static void halt(void) __attribute__((noreturn));
static void halt(void) {
    cpu_halt_forever();
    __builtin_unreachable();
}

static uint32_t read_cr2(void) {
    uint32_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

static uint16_t read_ss(void) {
    uint16_t ss;
    __asm__ __volatile__("mov %%ss, %0" : "=r"(ss));
    return ss;
}

static uint32_t read_le32(const uint8_t *value) {
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

/* Fatal output must not enter the display, formatter, mutex or scheduler
 * paths: any of those may be the corrupted subsystem that raised the panic.
 * The 16550 writer has a fixed poll limit and returns immediately when COM1
 * was not detected. Every string is capped even when supplied by a caller. */
static void panic_putc(char character) {
    serial_write_char(SERIAL_COM1, character);
}

static void panic_write(const char *text) {
    if (text == NULL) return;
    for (uint32_t index = 0U;
         index < PANIC_OUTPUT_TEXT_LIMIT && text[index] != '\0'; ++index)
        panic_putc(text[index]);
}

static void panic_write_hex(uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";
    if (digits > 8U) digits = 8U;
    for (uint32_t index = digits; index > 0U; --index)
        panic_putc(hex[(value >> ((index - 1U) * 4U)) & 0x0FU]);
}

static void panic_write_unsigned(uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U) panic_putc(digits[--count]);
}

static void panic_write_signed(int32_t value) {
    uint32_t magnitude = (uint32_t)value;
    if (value < 0) {
        panic_putc('-');
        magnitude = 0U - magnitude;
    }
    panic_write_unsigned(magnitude);
}

static void panic_write_hex_field(const char *label, uint32_t value,
                                  uint32_t digits) {
    panic_write(label);
    panic_write("0x");
    panic_write_hex(value, digits);
}

const char* kernel_build_id(void) {
    static const char hex[] = "0123456789ABCDEF";
    static const char unavailable[] = "unavailable";

    if (build_id_initialized) return build_id_text;

    uintptr_t note_start = (uintptr_t)_kernel_build_id_note_start;
    uintptr_t note_end = (uintptr_t)_kernel_build_id_note_end;
    if (note_end < note_start) return unavailable;

    const uint8_t *note = _kernel_build_id_note_start;
    size_t note_size = (size_t)(note_end - note_start);
    if (note_size < 16U) return unavailable;

    uint32_t name_size = read_le32(note);
    uint32_t id_size = read_le32(note + 4U);
    uint32_t note_type = read_le32(note + 8U);
    size_t id_offset = 12U + (((size_t)name_size + 3U) & ~3U);
    if (name_size != GNU_BUILD_ID_NAME_SIZE ||
        id_size != GNU_BUILD_ID_SHA1_SIZE ||
        note_type != GNU_BUILD_ID_NOTE_TYPE ||
        id_offset > note_size || id_size > note_size - id_offset ||
        note[12] != 'G' || note[13] != 'N' || note[14] != 'U' ||
        note[15] != '\0') {
        return unavailable;
    }

    const uint8_t *identifier = note + id_offset;
    for (size_t index = 0; index < GNU_BUILD_ID_SHA1_SIZE; ++index) {
        build_id_text[index * 2U] = hex[identifier[index] >> 4];
        build_id_text[index * 2U + 1U] = hex[identifier[index] & 0x0FU];
    }
    build_id_text[GNU_BUILD_ID_SHA1_SIZE * 2U] = '\0';
    build_id_initialized = 1;
    return build_id_text;
}

void panic_dump_exception_context(const Registers* registers, uint32_t cr2) {
    panic_write("  Build ID   : ");
    panic_write(kernel_build_id());
    panic_putc('\n');
    panic_write_hex_field("  CR2        : ", cr2, 8U);
    panic_putc('\n');

    if (registers == NULL) {
        panic_write("  Register frame: unavailable\n");
        return;
    }

    uint32_t fault_esp;
    uint32_t fault_ss;
    if ((registers->cs & 3U) != 0U) {
        fault_esp = registers->useresp;
        fault_ss = registers->ss;
    } else {
        /* PUSHA captured ESP after the uniform five-word exception frame. */
        fault_esp = registers->esp + EXCEPTION_KERNEL_FRAME_BYTES;
        fault_ss = read_ss();
    }

    panic_write_hex_field("  EAX=", registers->eax, 8U);
    panic_write_hex_field(" EBX=", registers->ebx, 8U);
    panic_write_hex_field(" ECX=", registers->ecx, 8U);
    panic_write_hex_field(" EDX=", registers->edx, 8U);
    panic_putc('\n');
    panic_write_hex_field("  ESI=", registers->esi, 8U);
    panic_write_hex_field(" EDI=", registers->edi, 8U);
    panic_write_hex_field(" EBP=", registers->ebp, 8U);
    panic_write_hex_field(" ESP=", fault_esp, 8U);
    panic_putc('\n');
    panic_write_hex_field("  EIP=", registers->eip, 8U);
    panic_write_hex_field(" EFLAGS=", registers->eflags, 8U);
    panic_putc('\n');
    panic_write_hex_field("  CS=", registers->cs & 0xFFFFU, 4U);
    panic_write_hex_field(" SS=", fault_ss & 0xFFFFU, 4U);
    panic_write_hex_field(" DS=", registers->ds & 0xFFFFU, 4U);
    panic_write_hex_field(" ES=", registers->es & 0xFFFFU, 4U);
    panic_write_hex_field(" FS=", registers->fs & 0xFFFFU, 4U);
    panic_write_hex_field(" GS=", registers->gs & 0xFFFFU, 4U);
    panic_putc('\n');
    panic_write("  VECTOR=");
    panic_write_unsigned(registers->irq_number);
    panic_write_hex_field(" ERROR=", registers->error_code, 8U);
    panic_putc('\n');
}

static void panic_rule(void) {
    panic_write("-------------------------------------------------------------------------------\n");
}

static void panic_header(const char *title) {
    panic_rule();
    panic_write("                            ");
    panic_write(title);
    panic_putc('\n');
    panic_rule();
    panic_write("\n");
    panic_write("  The operating system encountered a fatal error and cannot continue.\n");
    panic_write("  Your files on disk have not been modified by this panic handler.\n\n");
}

static void panic_label(const char *label) {
    panic_write("  ");
    panic_write(label);
    panic_putc('\n');
}

static void panic_dump_failure_context(uintptr_t caller) {
    panic_context_record_t context;
    panic_label("FAILURE CONTEXT");
    if (!panic_context_snapshot(&context)) {
        panic_write("  Diagnostic context: unavailable\n");
    } else {
        panic_write("  Phase      : ");
        panic_write(context.phase[0] ? context.phase : "unknown");
        panic_putc('\n');
        panic_write("  Component  : ");
        panic_write(context.component[0] ? context.component : "unknown");
        panic_putc('\n');
        panic_write("  Operation  : ");
        panic_write(context.operation[0] ? context.operation : "unknown");
        panic_putc('\n');
        panic_write("  Subject    : ");
        panic_write(context.subject[0] ? context.subject : "none");
        panic_putc('\n');
        if (context.has_result != 0U) {
            panic_write("  Result     : ");
            panic_write_signed(context.result);
            panic_putc('\n');
            panic_write_hex_field("  Details    : ", context.detail0, 8U);
            panic_write_hex_field(" ", context.detail1, 8U);
            panic_putc('\n');
        }
        panic_write("  Sequence   : ");
        panic_write_unsigned(context.sequence);
        panic_putc('\n');
    }
    if (caller != 0U) {
        panic_write_hex_field("  Panic call : ", (uint32_t)caller, 8U);
        panic_putc('\n');
    }
}

static void panic_footer(void) {
    panic_putc('\n');
    panic_rule();
    panic_write("  ACTION REQUIRED\n");
    panic_write("  Restart the computer. If this error repeats, record the message above.\n");
    panic_rule();
}

/**
 * Kernel panic - unrecoverable error
 */
void __attribute__((noreturn)) panic(const char* message) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    // Disable interrupts immediately
    irq_disable();
    
    // Check for recursive or concurrent panic before diagnostic side effects.
    if (__sync_lock_test_and_set(&panic_in_progress, 1U) != 0U) {
        halt();
    }
    
    panic_header("KERNEL PANIC");
    panic_label("ERROR");
    panic_write("  ");
    panic_write(message ? message : "Unknown kernel error");
    panic_putc('\n');
    panic_dump_failure_context(caller);
    panic_label("CPU STATE");
    panic_dump_exception_context(NULL, read_cr2());
    panic_footer();
    
    halt();
}

void __attribute__((noreturn)) panic_with_exception(
    const char* message, const Registers* registers, uint32_t cr2) {
    irq_disable();

    if (__sync_lock_test_and_set(&panic_in_progress, 1U) != 0U) {
        halt();
    }

    panic_header("KERNEL PANIC");
    panic_label("ERROR");
    panic_write("  ");
    panic_write(message ? message : "Unknown CPU exception");
    panic_putc('\n');
    panic_dump_failure_context(0U);
    panic_label("CPU STATE");
    panic_dump_exception_context(registers, cr2);
    panic_footer();

    halt();
}

/**
 * Kernel assertion failure
 */
void __attribute__((noreturn)) kassert_fail(const char* expr, const char* file,
                                              int line, const char* func) {
    // Disable interrupts immediately
    irq_disable();
    
    // Check for recursive or concurrent panic before diagnostic side effects.
    if (__sync_lock_test_and_set(&panic_in_progress, 1U) != 0U) {
        halt();
    }
    
    panic_header("KERNEL ASSERTION FAILED");
    panic_label("FAILED CHECK");
    panic_write("  Expression : ");
    panic_write(expr ? expr : "Unknown");
    panic_putc('\n');
    panic_write("  Source     : ");
    panic_write(file ? file : "Unknown");
    panic_putc(':');
    panic_write_signed(line);
    panic_putc('\n');
    panic_write("  Function   : ");
    panic_write(func ? func : "Unknown");
    panic_putc('\n');
    panic_dump_failure_context(0U);
    panic_label("CPU STATE");
    panic_dump_exception_context(NULL, read_cr2());
    panic_footer();
    
    halt();
}
