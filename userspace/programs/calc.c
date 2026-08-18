/**
 * @file userspace/programs/calc.c
 * @brief Berechnet einfache Integerausdrücke.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

#define INPUT_CAPACITY 24

static int read_integer(const char *prompt, int *result) {
    char input[INPUT_CAPACITY];
    unsigned int length = 0;
    int sign = 1;
    int value = 0;

    x86os_puts(prompt);
    for (;;) {
        char ch = (char)x86os_getchar();
        if (ch == '\r' || ch == '\n') {
            x86os_putchar('\n');
            break;
        }
        if ((ch >= '0' && ch <= '9') || (ch == '-' && length == 0)) {
            if (length + 1U < INPUT_CAPACITY) {
                input[length++] = ch;
                x86os_putchar(ch);
            }
        }
    }
    if (length == 0) return -1;
    unsigned int index = 0;
    if (input[0] == '-') {
        sign = -1;
        index = 1;
        if (length == 1) return -1;
    }
    for (; index < length; ++index) {
        value = value * 10 + (input[index] - '0');
    }
    *result = value * sign;
    return 0;
}

static void print_integer(int value) {
    char digits[12];
    unsigned int count = 0;
    unsigned int magnitude;

    if (value < 0) {
        x86os_putchar('-');
        magnitude = 0U - (unsigned int)value;
    } else {
        magnitude = (unsigned int)value;
    }
    do {
        digits[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(void) {
    int left;
    int right;
    int result;

    x86os_puts("Integer calculator (+, -, *, /)\n");
    if (read_integer("First number: ", &left) != 0) return 1;
    x86os_puts("Operator: ");
    char operation = (char)x86os_getchar();
    x86os_putchar(operation);
    x86os_putchar('\n');
    if (read_integer("Second number: ", &right) != 0) return 1;

    if (operation == '+') result = left + right;
    else if (operation == '-') result = left - right;
    else if (operation == '*') result = left * right;
    else if (operation == '/') {
        if (right == 0) {
            x86os_puts("Error: division by zero\n");
            return 1;
        }
        result = left / right;
    } else {
        x86os_puts("Error: unsupported operator\n");
        return 1;
    }

    x86os_puts("Result: ");
    print_integer(result);
    x86os_putchar('\n');
    return 0;
}
