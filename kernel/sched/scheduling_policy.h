#ifndef SCHEDULING_POLICY_H
#define SCHEDULING_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCHEDULER_CLASS_AMBIENT 0U
#define SCHEDULER_CLASS_SERVICE 1U
#define SCHEDULER_CLASS_SAFETY  2U

#define SCHEDULER_BUDGET_AMBIENT 1U
#define SCHEDULER_BUDGET_SERVICE 1U
#define SCHEDULER_BUDGET_SAFETY  2U
#define SCHEDULER_POLICY_MAX_CANDIDATES 32U

typedef struct {
    bool runnable;
    uint8_t scheduling_class;
    uint8_t budget_remaining;
} scheduler_candidate_t;

uint8_t scheduler_policy_budget(uint8_t scheduling_class);
int scheduler_policy_select(scheduler_candidate_t *candidates, size_t count,
                            int after);

#endif
