/**
 * @file kernel/sched/scheduling_policy.c
 * @brief Begrenzte Scheduling-Auswahl und Budgetlogik.
 *
 * Layer: Ring-0 scheduler.
 * Contract: Die Auswahl iteriert nur über die feste Prozesskapazität.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#include "kernel/sched/scheduling_policy.h"

#include <limits.h>

uint32_t scheduler_policy_window_limit(uint8_t scheduling_class) {
    switch (scheduling_class) {
        case SCHEDULER_CLASS_AMBIENT: return SCHEDULER_WINDOW_AMBIENT_MS;
        case SCHEDULER_CLASS_SERVICE: return SCHEDULER_WINDOW_SERVICE_MS;
        case SCHEDULER_CLASS_SAFETY: return SCHEDULER_WINDOW_SAFETY_MS;
        default: return 0U;
    }
}

static uint32_t saturating_add_u32(uint32_t value, uint64_t amount) {
    if (amount >= UINT32_MAX || value > UINT32_MAX - (uint32_t)amount)
        return UINT32_MAX;
    return value + (uint32_t)amount;
}

static void record_charge(scheduler_window_t *window,
                          uint8_t scheduling_class, uint64_t elapsed_ms) {
    uint32_t limit = scheduler_policy_window_limit(scheduling_class);
    if (limit == 0U || elapsed_ms == 0U) return;
    uint8_t bit = (uint8_t)(1U << scheduling_class);
    uint32_t before = window->used_ms[scheduling_class];
    window->used_ms[scheduling_class] = saturating_add_u32(before, elapsed_ms);
    if (window->used_ms[scheduling_class] >= limit) {
        if ((window->throttled_mask & bit) == 0U) {
            window->overload_count[scheduling_class] = saturating_add_u32(
                window->overload_count[scheduling_class], 1U);
        }
        window->throttled_mask |= bit;
    }
}

void scheduler_policy_window_init(scheduler_window_t *window,
                                  uint64_t now_ms) {
    if (window == NULL) return;
    *window = (scheduler_window_t){0};
    window->window_start_ms = now_ms - now_ms % SCHEDULER_WINDOW_MS;
    window->last_account_ms = now_ms;
    window->initialized = true;
}

bool scheduler_policy_window_charge(scheduler_window_t *window,
                                    uint8_t scheduling_class,
                                    uint64_t now_ms) {
    if (window == NULL) return false;
    if (!window->initialized) {
        scheduler_policy_window_init(window, now_ms);
        return false;
    }
    if (now_ms < window->last_account_ms) {
        /* A regressing clock invalidates all accounting. This fault remains
         * latched until an explicit scheduler_policy_window_init(). */
        window->clock_anomaly_count = saturating_add_u32(
            window->clock_anomaly_count, 1U);
        window->fault_flags |= SCHEDULER_WINDOW_FAULT_CLOCK_REGRESSION;
        window->throttled_mask = (1U << SCHEDULER_CLASS_COUNT) - 1U;
        return false;
    }

    uint64_t new_start = now_ms - now_ms % SCHEDULER_WINDOW_MS;
    bool rolled = new_start != window->window_start_ms;
    if (rolled) {
        uint64_t old_end = window->window_start_ms <=
                UINT64_MAX - SCHEDULER_WINDOW_MS
            ? window->window_start_ms + SCHEDULER_WINDOW_MS : UINT64_MAX;
        if (old_end > window->last_account_ms &&
            scheduling_class < SCHEDULER_CLASS_COUNT) {
            record_charge(window, scheduling_class,
                          old_end - window->last_account_ms);
        }
        uint64_t skipped_windows = new_start > old_end
            ? (new_start - old_end) / SCHEDULER_WINDOW_MS : 0U;
        if (scheduling_class < SCHEDULER_CLASS_COUNT &&
            skipped_windows != 0U) {
            window->overload_count[scheduling_class] = saturating_add_u32(
                window->overload_count[scheduling_class], skipped_windows);
        }
        uint32_t overload[SCHEDULER_CLASS_COUNT];
        uint32_t clock_anomaly_count = window->clock_anomaly_count;
        uint32_t fault_flags = window->fault_flags;
        for (size_t index = 0U; index < SCHEDULER_CLASS_COUNT; ++index)
            overload[index] = window->overload_count[index];
        *window = (scheduler_window_t){0};
        for (size_t index = 0U; index < SCHEDULER_CLASS_COUNT; ++index)
            window->overload_count[index] = overload[index];
        window->clock_anomaly_count = clock_anomaly_count;
        window->fault_flags = fault_flags;
        if (fault_flags != 0U)
            window->throttled_mask = (1U << SCHEDULER_CLASS_COUNT) - 1U;
        window->window_start_ms = new_start;
        window->initialized = true;
        if (scheduling_class < SCHEDULER_CLASS_COUNT)
            record_charge(window, scheduling_class, now_ms - new_start);
    } else if (scheduling_class < SCHEDULER_CLASS_COUNT) {
        record_charge(window, scheduling_class,
                      now_ms - window->last_account_ms);
    }
    window->last_account_ms = now_ms;
    return rolled;
}

bool scheduler_policy_class_allowed(const scheduler_window_t *window,
                                    uint8_t scheduling_class) {
    if (window == NULL || !window->initialized ||
        scheduling_class >= SCHEDULER_CLASS_COUNT)
        return false;
    if (window->fault_flags != 0U) return false;
    return (window->throttled_mask & (1U << scheduling_class)) == 0U;
}

bool scheduler_policy_background_allowed(const scheduler_window_t *window,
                                         uint8_t scheduling_class) {
    /* Only the selection fallback may use this predicate. Funded work must
     * have been exhausted first; no donor budget is changed or transferred.
     * Safety overload and clock faults retain the kernel recovery interval. */
    return scheduling_class < SCHEDULER_CLASS_SAFETY &&
        scheduler_policy_class_allowed(window, SCHEDULER_CLASS_SAFETY) &&
        !scheduler_policy_class_allowed(window, scheduling_class);
}

#ifdef REIST_RUNTIME_DEGRADATION_FAULT_INJECTION
bool scheduler_policy_degradation_self_test(void) {
    scheduler_window_t window;
    scheduler_policy_window_init(&window, 250U);
    if (scheduler_policy_window_charge(&window, SCHEDULER_CLASS_SAFETY,
                                       249U) ||
        window.clock_anomaly_count != 1U ||
        (window.fault_flags & SCHEDULER_WINDOW_FAULT_CLOCK_REGRESSION) == 0U)
        return false;
    for (uint8_t scheduling_class = 0U;
         scheduling_class < SCHEDULER_CLASS_COUNT; ++scheduling_class) {
        if (scheduler_policy_class_allowed(&window, scheduling_class))
            return false;
    }
    if (!scheduler_policy_window_charge(&window, SCHEDULER_CLASS_SAFETY,
                                        500U) ||
        scheduler_policy_class_allowed(&window, SCHEDULER_CLASS_SAFETY))
        return false;
    window.clock_anomaly_count = UINT32_MAX;
    window.last_account_ms = 500U;
    (void)scheduler_policy_window_charge(&window, SCHEDULER_CLASS_SAFETY,
                                         499U);
    return window.clock_anomaly_count == UINT32_MAX;
}
#endif

void scheduler_policy_inherit(uint8_t *effective_classes,
                              const uint8_t *base_classes,
                              const int8_t *blocked_owners, size_t count) {
    if (effective_classes == NULL || base_classes == NULL ||
        blocked_owners == NULL || count == 0U ||
        count > SCHEDULER_POLICY_MAX_CANDIDATES) return;
    bool has_valid_owner = false;
    for (size_t index = 0U; index < count; ++index) {
        effective_classes[index] =
            scheduler_policy_budget(base_classes[index]) != 0U
                ? base_classes[index] : SCHEDULER_CLASS_AMBIENT;
        int owner = blocked_owners[index];
        if (owner >= 0 && owner < (int)count && owner != (int)index)
            has_valid_owner = true;
    }
    if (!has_valid_owner) return;
    /* At most count-1 inheritance edges can contribute to a simple chain.
     * Repeating exactly count times also converges safely in a cycle. */
    for (size_t pass = 0U; pass < count; ++pass) {
        for (size_t waiter = 0U; waiter < count; ++waiter) {
            int owner = blocked_owners[waiter];
            if (owner < 0 || owner >= (int)count || owner == (int)waiter)
                continue;
            if (effective_classes[waiter] > effective_classes[owner])
                effective_classes[owner] = effective_classes[waiter];
        }
    }
}

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

int scheduler_policy_select_cycle(
        scheduler_candidate_t *candidates, size_t count,
        int8_t class_cursors[SCHEDULER_CLASS_COUNT], uint8_t *cycle_cursor) {
    static const uint8_t cycle[] = {
        SCHEDULER_CLASS_SAFETY, SCHEDULER_CLASS_SAFETY,
        SCHEDULER_CLASS_SERVICE, SCHEDULER_CLASS_AMBIENT
    };
    if (candidates == NULL || class_cursors == NULL || cycle_cursor == NULL ||
        count == 0U || count > SCHEDULER_POLICY_MAX_CANDIDATES) return -1;
    if (*cycle_cursor >= sizeof(cycle)) *cycle_cursor = 0U;
    for (size_t class_step = 0U; class_step < sizeof(cycle); ++class_step) {
        uint8_t position = (uint8_t)((*cycle_cursor + class_step) %
                                     sizeof(cycle));
        uint8_t scheduling_class = cycle[position];
        int after = class_cursors[scheduling_class];
        if (after < -1 || after >= (int)count) after = -1;
        for (size_t task_step = 1U; task_step <= count; ++task_step) {
            size_t index = after < 0 ? task_step - 1U :
                ((size_t)after + task_step) % count;
            scheduler_candidate_t *candidate = &candidates[index];
            if (candidate->runnable &&
                candidate->scheduling_class == scheduling_class) {
                class_cursors[scheduling_class] = (int8_t)index;
                *cycle_cursor = (uint8_t)((position + 1U) % sizeof(cycle));
                return (int)index;
            }
        }
    }
    return -1;
}
