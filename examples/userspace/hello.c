#include "x86os.h"

static volatile uint32_t initialized_value = 41;
static volatile uint32_t zero_initialized_value;
static const char success_message[] = "USERSPACE-E2E-OK\n";
static const char failure_message[] = "USERSPACE-E2E-FAILED\n";

int main(void) {
    initialized_value++;
    if (initialized_value == 42 && zero_initialized_value == 0) {
        x86os_puts(success_message);
        return 0;
    }
    x86os_puts(failure_message);
    return 1;
}
