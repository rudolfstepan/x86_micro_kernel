#include "kernel/sched/scheduling_policy.h"

#include <stdio.h>

#define CHECK(condition, code) do { if (!(condition)) return (code); } while (0)

int main(void) {
    scheduler_candidate_t candidates[] = {
        {true, SCHEDULER_CLASS_AMBIENT, SCHEDULER_BUDGET_AMBIENT},
        {true, SCHEDULER_CLASS_SERVICE, SCHEDULER_BUDGET_SERVICE},
        {true, SCHEDULER_CLASS_SAFETY, SCHEDULER_BUDGET_SAFETY},
        {true, SCHEDULER_CLASS_AMBIENT, SCHEDULER_BUDGET_AMBIENT},
    };
    const int expected[] = {2, 2, 1, 3, 0, 2};
    int after = -1;
    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]);
         ++index) {
        after = scheduler_policy_select(candidates, 4U, after);
        CHECK(after == expected[index], 10 + (int)index);
    }

    candidates[2].runnable = false;
    candidates[1].budget_remaining = 0U;
    candidates[0].budget_remaining = 0U;
    candidates[3].budget_remaining = 0U;
    after = scheduler_policy_select(candidates, 4U, 2);
    CHECK(after == 1, 30);
    CHECK(scheduler_policy_budget(99U) == 0U, 31);
    CHECK(scheduler_policy_select(NULL, 4U, 0) == -1, 32);
    CHECK(scheduler_policy_select(candidates, 4U, 4) == -1, 33);

    scheduler_window_t window;
    scheduler_policy_window_init(&window, 95U);
    CHECK(scheduler_policy_window_charge(
              &window, SCHEDULER_CLASS_AMBIENT, 100U), 40);
    CHECK(window.used_ms[SCHEDULER_CLASS_AMBIENT] == 0U, 41);
    CHECK(scheduler_policy_window_charge(
              &window, SCHEDULER_CLASS_AMBIENT, 115U) == false, 42);
    CHECK(!scheduler_policy_class_allowed(
              &window, SCHEDULER_CLASS_AMBIENT), 43);
    CHECK(window.overload_count[SCHEDULER_CLASS_AMBIENT] == 1U, 44);
    CHECK(scheduler_policy_class_allowed(
              &window, SCHEDULER_CLASS_SERVICE), 45);
    CHECK(scheduler_policy_window_charge(
              &window, SCHEDULER_CLASS_NONE, 200U), 46);
    CHECK(scheduler_policy_class_allowed(
              &window, SCHEDULER_CLASS_AMBIENT), 47);
    CHECK(window.overload_count[SCHEDULER_CLASS_AMBIENT] == 1U, 48);

    scheduler_policy_window_init(&window, 250U);
    CHECK(!scheduler_policy_window_charge(
              &window, SCHEDULER_CLASS_SAFETY, 249U), 49);
    CHECK(!scheduler_policy_class_allowed(
              &window, SCHEDULER_CLASS_AMBIENT) &&
          !scheduler_policy_class_allowed(
              &window, SCHEDULER_CLASS_SERVICE) &&
          !scheduler_policy_class_allowed(
              &window, SCHEDULER_CLASS_SAFETY), 50);

    scheduler_policy_window_init(&window, 100U);
    CHECK(scheduler_policy_window_charge(
              &window, SCHEDULER_CLASS_SERVICE, 500U), 51);
    CHECK(window.overload_count[SCHEDULER_CLASS_SERVICE] == 4U, 52);

    puts("SCHEDULING_POLICY_OK");
    return 0;
}
