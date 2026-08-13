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

extern const uint8_t _kernel_build_id_note_start[];
extern const uint8_t _kernel_build_id_note_end[];

// Prevent recursive panics
static int panic_in_progress = 0;
static char build_id_text[GNU_BUILD_ID_SHA1_SIZE * 2U + 1U];
static int build_id_initialized = 0;

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
    panic_label("CPU STATE");
    panic_dump_exception_context(NULL, read_cr2());
    panic_footer();
    
    halt();
}
