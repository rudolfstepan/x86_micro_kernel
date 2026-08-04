#include "x86os.h"

static volatile uint32_t initialized_value = 41;
static volatile uint32_t zero_initialized_value;
static const char success_message[] = "USERSPACE-E2E-OK\n";
static const char failure_message[] = "USERSPACE-E2E-FAILED\n";

int main(void) {
    initialized_value++;
    char *memory = (char*)x86os_malloc(8);
    if (memory != 0) {
        memory[0] = 'O';
        memory[1] = 'K';
        memory = (char*)x86os_realloc(memory, 8192);
    }
    if (initialized_value == 42 && zero_initialized_value == 0 &&
        memory != 0 && memory[0] == 'O' && memory[1] == 'K') {
        x86os_free(memory);
        x86os_puts(success_message);
        return 0;
    }
    x86os_free(memory);
    x86os_puts(failure_message);
    return 1;
}
