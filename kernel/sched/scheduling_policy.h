/**
 * @file kernel/sched/scheduling_policy.h
 * @brief Schnittstelle der Scheduling-Policy.
 *
 * Layer: Ring-0 scheduler.
 * Contract: Die Policy verändert keine Prozessressourcen.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
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
#define SCHEDULER_WINDOW_MS 100U
#define SCHEDULER_WINDOW_AMBIENT_MS 15U
#define SCHEDULER_WINDOW_SERVICE_MS 25U
#define SCHEDULER_WINDOW_SAFETY_MS 60U
#define SCHEDULER_CLASS_COUNT 3U
#define SCHEDULER_CLASS_NONE UINT8_MAX
#define SCHEDULER_WINDOW_FAULT_CLOCK_REGRESSION (1U << 0U)

typedef struct {
    bool runnable;
    uint8_t scheduling_class;
    uint8_t budget_remaining;
} scheduler_candidate_t;

typedef struct {
    uint64_t window_start_ms;
    uint64_t last_account_ms;
    uint32_t used_ms[SCHEDULER_CLASS_COUNT];
    uint32_t overload_count[SCHEDULER_CLASS_COUNT];
    uint32_t clock_anomaly_count;
    uint32_t fault_flags;
    uint8_t throttled_mask;
    bool initialized;
} scheduler_window_t;

uint8_t scheduler_policy_budget(uint8_t scheduling_class);
int scheduler_policy_select(scheduler_candidate_t *candidates, size_t count,
                            int after);
int scheduler_policy_select_cycle(
    scheduler_candidate_t *candidates, size_t count,
    int8_t class_cursors[SCHEDULER_CLASS_COUNT], uint8_t *cycle_cursor);
void scheduler_policy_window_init(scheduler_window_t *window,
                                  uint64_t now_ms);
bool scheduler_policy_window_charge(scheduler_window_t *window,
                                    uint8_t scheduling_class,
                                    uint64_t now_ms);
bool scheduler_policy_class_allowed(const scheduler_window_t *window,
                                    uint8_t scheduling_class);
/* Internal background contract v1, not a reservation transfer or new ABI. */
bool scheduler_policy_background_allowed(const scheduler_window_t *window,
                                         uint8_t scheduling_class);
uint32_t scheduler_policy_window_limit(uint8_t scheduling_class);
void scheduler_policy_inherit(uint8_t *effective_classes,
                              const uint8_t *base_classes,
                              const int8_t *blocked_owners, size_t count);

#ifdef REIST_RUNTIME_DEGRADATION_FAULT_INJECTION
bool scheduler_policy_degradation_self_test(void);
#endif

#endif
