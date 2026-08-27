/**
 * @file userspace/programs/capacity_child.c
 * @brief Bounded child that remains alive during task-capacity admission tests.
 *
 * Layer: Ring-3 test program.
 * Contract: The parent normally terminates this process after probing the
 * supervised task reserve. The finite deadline prevents an orphaned test
 * process from waiting forever if the parent fails.
 */
#include "x86os.h"

#define CAPACITY_CHILD_DEADLINE_MS 10000U

int main(void) {
    return x86os_sleep_ms(CAPACITY_CHILD_DEADLINE_MS) == 0 ? 42 : 77;
}
