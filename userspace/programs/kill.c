#include "x86os.h"

static int parse_pid(const char* text, int* pid) {
    unsigned value = 0;
    if (text == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') return -1;
        value = value * 10U + (unsigned)(*text++ - '0');
        if (value > 2147483647U) return -1;
    }
    if (value == 0U) return -1;
    *pid = (int)value;
    return 0;
}

int main(int argc, char** argv) {
    int pid;
    if (argc != 2 || parse_pid(argv[1], &pid) != 0) {
        x86os_puts("Usage: kill <pid>\n");
        return 2;
    }
    if (x86os_kill(pid) < 0) {
        x86os_puts("Unable to terminate process.\n");
        return 1;
    }
    return 0;
}
