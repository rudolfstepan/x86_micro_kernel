#include "kernel/sched/scheduling_policy.h"

uint8_t scheduler_policy_budget(uint8_t scheduling_class) {
    switch (scheduling_class) {
        case SCHEDULER_CLASS_AMBIENT: return SCHEDULER_BUDGET_AMBIENT;
        case SCHEDULER_CLASS_SERVICE: return SCHEDULER_BUDGET_SERVICE;
        case SCHEDULER_CLASS_SAFETY: return SCHEDULER_BUDGET_SAFETY;
        default: return 0U;
    }
}

static int highest_runnable_class(const scheduler_candidate_t *candidates,
                                  size_t count) {
    int highest = -1;
    for (size_t index = 0U; index < count; ++index) {
        const scheduler_candidate_t *candidate = &candidates[index];
        if (!candidate->runnable || candidate->budget_remaining == 0U ||
            scheduler_policy_budget(candidate->scheduling_class) == 0U)
            continue;
        if ((int)candidate->scheduling_class > highest)
            highest = (int)candidate->scheduling_class;
    }
    return highest;
}

int scheduler_policy_select(scheduler_candidate_t *candidates, size_t count,
                            int after) {
    if (candidates == NULL || count == 0U ||
        count > SCHEDULER_POLICY_MAX_CANDIDATES || after < -1 ||
        after >= (int)count) return -1;

    int highest = highest_runnable_class(candidates, count);
    if (highest < 0) {
        /* A round is replenished only after every runnable task exhausted its
         * fixed share. Blocked tasks cannot delay replenishment. */
        for (size_t index = 0U; index < count; ++index) {
            candidates[index].budget_remaining = scheduler_policy_budget(
                candidates[index].scheduling_class);
        }
        highest = highest_runnable_class(candidates, count);
    }
    if (highest < 0) return -1;

    for (size_t step = 1U; step <= count; ++step) {
        size_t index = after < 0 ? step - 1U :
            ((size_t)after + step) % count;
        scheduler_candidate_t *candidate = &candidates[index];
        if (candidate->runnable && candidate->budget_remaining != 0U &&
            candidate->scheduling_class == (uint8_t)highest) {
            --candidate->budget_remaining;
            return (int)index;
        }
    }
    return -1;
}
