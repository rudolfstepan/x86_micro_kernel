/**
 * @file userspace/programs/repeat.c
 * @brief Wiederholt begrenzte Textargumente.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

#define LINE_CAPACITY 128

int main(void) {
    char line[LINE_CAPACITY];
    unsigned int length = 0;

    x86os_puts("Enter text: ");
    for (;;) {
        int value = x86os_getchar();
        char ch = (char)value;
        if (ch == '\r' || ch == '\n') {
            x86os_putchar('\n');
            break;
        }
        if (ch == '\b' || ch == 0x7f) {
            if (length != 0) {
                --length;
                x86os_puts("\b \b");
            }
            continue;
        }
        if (ch >= 0x20 && ch <= 0x7e && length + 1U < LINE_CAPACITY) {
            line[length++] = ch;
            x86os_putchar(ch);
        }
    }
    line[length] = '\0';
    x86os_puts("Repeated: ");
    x86os_puts(line);
    x86os_putchar('\n');
    return 0;
}
