/**
 * @file panic.c
 * @brief Kernel panic handler implementation
 */

#include "include/kernel/panic.h"
#include "arch/x86/include/interrupt.h"
#include "lib/libc/stdio.h"
#include "drivers/video/display.h"

#include <stddef.h>
#include <stdint.h>

#define GNU_BUILD_ID_NAME_SIZE 4U
#define GNU_BUILD_ID_SHA1_SIZE 20U
#define GNU_BUILD_ID_NOTE_TYPE 3U
#define EXCEPTION_KERNEL_FRAME_BYTES 20U
#define PANIC_CONTEXT_TEXT_CAPACITY 32U
#define PANIC_CONTEXT_VERSION 1U

extern const uint8_t _kernel_build_id_note_start[];
extern const uint8_t _kernel_build_id_note_end[];

// Prevent recursive panics
static int panic_in_progress = 0;
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
    printf("  Build ID   : %s\n", kernel_build_id());
    printf("  CR2        : 0x%08X\n", cr2);

    if (registers == NULL) {
        printf("  Register frame: unavailable\n");
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

    printf("  EAX=%08X EBX=%08X ECX=%08X EDX=%08X\n",
           registers->eax, registers->ebx, registers->ecx, registers->edx);
    printf("  ESI=%08X EDI=%08X EBP=%08X ESP=%08X\n",
           registers->esi, registers->edi, registers->ebp, fault_esp);
    printf("  EIP=%08X EFLAGS=%08X\n", registers->eip, registers->eflags);
    printf("  CS=%04X SS=%04X DS=%04X ES=%04X FS=%04X GS=%04X\n",
           registers->cs & 0xFFFFU, fault_ss & 0xFFFFU,
           registers->ds & 0xFFFFU, registers->es & 0xFFFFU,
           registers->fs & 0xFFFFU, registers->gs & 0xFFFFU);
    printf("  VECTOR=%u ERROR=0x%08X\n",
           registers->irq_number, registers->error_code);
}

static void panic_rule(void) {
    display_set_color(LIGHT_RED);
    printf("-------------------------------------------------------------------------------\n");
}

static void panic_header(const char *title) {
    display_set_color(WHITE);
    display_clear();
    panic_rule();
    display_set_color(LIGHT_RED);
    printf("                            %s\n", title);
    panic_rule();
    printf("\n");
    display_set_color(WHITE);
    printf("  The operating system encountered a fatal error and cannot continue.\n");
    printf("  Your files on disk have not been modified by this panic handler.\n\n");
}

static void panic_label(const char *label) {
    display_set_color(LIGHT_RED);
    printf("  %s\n", label);
    display_set_color(WHITE);
}

static void panic_dump_failure_context(uintptr_t caller) {
    panic_context_record_t context;
    panic_label("FAILURE CONTEXT");
    if (!panic_context_snapshot(&context)) {
        printf("  Diagnostic context: unavailable\n");
    } else {
        printf("  Phase      : %s\n", context.phase[0] ? context.phase : "unknown");
        printf("  Component  : %s\n",
               context.component[0] ? context.component : "unknown");
        printf("  Operation  : %s\n",
               context.operation[0] ? context.operation : "unknown");
        printf("  Subject    : %s\n",
               context.subject[0] ? context.subject : "none");
        if (context.has_result != 0U) {
            printf("  Result     : %d\n", context.result);
            printf("  Details    : 0x%08X 0x%08X\n",
                   context.detail0, context.detail1);
        }
        printf("  Sequence   : %u\n", context.sequence);
    }
    if (caller != 0U) printf("  Panic call : 0x%08X\n", (uint32_t)caller);
}

static void panic_footer(void) {
    printf("\n");
    panic_rule();
    display_set_color(YELLOW);
    printf("  ACTION REQUIRED\n");
    display_set_color(WHITE);
    printf("  Restart the computer. If this error repeats, record the message above.\n");
    panic_rule();
}

/**
 * Kernel panic - unrecoverable error
 */
void __attribute__((noreturn)) panic(const char* message) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    // Disable interrupts immediately
    irq_disable();
    
    // Check for recursive panic
    if (panic_in_progress) {
        halt();
    }
    panic_in_progress = 1;
    
    panic_header("KERNEL PANIC");
    panic_label("ERROR");
    printf("  %s\n", message ? message : "Unknown kernel error");
    panic_dump_failure_context(caller);
    panic_label("CPU STATE");
    panic_dump_exception_context(NULL, read_cr2());
    panic_footer();
    
    halt();
}

void __attribute__((noreturn)) panic_with_exception(
    const char* message, const Registers* registers, uint32_t cr2) {
    irq_disable();

    if (panic_in_progress) {
        halt();
    }
    panic_in_progress = 1;

    panic_header("KERNEL PANIC");
    panic_label("ERROR");
    printf("  %s\n", message ? message : "Unknown CPU exception");
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
    
    // Check for recursive panic
    if (panic_in_progress) {
        halt();
    }
    panic_in_progress = 1;
    
    panic_header("KERNEL ASSERTION FAILED");
    panic_label("FAILED CHECK");
    printf("  Expression : %s\n", expr ? expr : "Unknown");
    printf("  Source     : %s:%d\n", file ? file : "Unknown", line);
    printf("  Function   : %s\n", func ? func : "Unknown");
    panic_dump_failure_context(0U);
    panic_label("CPU STATE");
    panic_dump_exception_context(NULL, read_cr2());
    panic_footer();
    
    halt();
}
