/**
 * @file userspace/programs/ps.c
 * @brief Listet sichtbare Prozesse.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

static void print_uint(unsigned value, unsigned width) {
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (width > count) {
        x86os_putchar(' ');
        --width;
    }
    while (count != 0U) x86os_putchar(digits[--count]);
}

static const char* state_name(int state) {
    switch (state) {
        case X86OS_PROCESS_RUNNING: return "RUNNING ";
        case X86OS_PROCESS_SLEEPING: return "SLEEPING";
        case X86OS_PROCESS_WAITING: return "WAITING ";
        case X86OS_PROCESS_ZOMBIE: return "ZOMBIE  ";
        default: return "READY   ";
    }
}

int main(void) {
    x86os_puts("  PID  PPID  STATE     NAME\n");
    for (uint32_t index = 0;; ++index) {
        x86os_process_info_t info;
        int result = x86os_process_info(index, &info);
        if (result == 0) break;
        if (result < 0) {
            x86os_puts("Unable to read process table.\n");
            return 1;
        }
        print_uint((unsigned)info.pid, 5);
        print_uint((unsigned)info.parent_pid, 6);
        x86os_puts("  ");
        x86os_puts(state_name(info.state));
        x86os_puts("  ");
        x86os_puts(info.name);
        x86os_putchar('\n');
    }
    return 0;
}
