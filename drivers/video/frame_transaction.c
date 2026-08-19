/**
 * @file drivers/video/frame_transaction.c
 * @brief Bounded owner/generation frame transaction state machine.
 */
#include "frame_transaction.h"

#include <stddef.h>
#include <stdint.h>

#define FRAME_EACCES (-13)
#define FRAME_EBUSY (-16)
#define FRAME_EINVAL (-22)
#define FRAME_ETIMEDOUT (-110)
#define FRAME_ESTALE (-116)

static bool identity_valid(int pid, uint32_t generation) {
    return pid > 0 && generation != 0U;
}

static bool same_identity(int left_pid, uint32_t left_generation,
                          int right_pid, uint32_t right_generation) {
    return left_pid == right_pid && left_generation == right_generation;
}

static bool expired(const display_frame_transaction_t *state,
                    uint64_t now_ms) {
    return state->active && now_ms >= state->deadline_ms;
}

static bool rect_intersects(const display_frame_rect_t *left,
                            const display_frame_rect_t *right) {
    uint64_t left_right = (uint64_t)left->x + left->width;
    uint64_t left_bottom = (uint64_t)left->y + left->height;
    uint64_t right_right = (uint64_t)right->x + right->width;
    uint64_t right_bottom = (uint64_t)right->y + right->height;
    return (uint64_t)left->x < right_right &&
           (uint64_t)right->x < left_right &&
           (uint64_t)left->y < right_bottom &&
           (uint64_t)right->y < left_bottom;
}

static display_frame_rect_t rect_union(const display_frame_rect_t *left,
                                       const display_frame_rect_t *right) {
    uint32_t x = left->x < right->x ? left->x : right->x;
    uint32_t y = left->y < right->y ? left->y : right->y;
    uint64_t left_right = (uint64_t)left->x + left->width;
    uint64_t right_right = (uint64_t)right->x + right->width;
    uint64_t left_bottom = (uint64_t)left->y + left->height;
    uint64_t right_bottom = (uint64_t)right->y + right->height;
    uint64_t maximum_x = left_right > right_right ? left_right : right_right;
    uint64_t maximum_y = left_bottom > right_bottom
        ? left_bottom : right_bottom;
    display_frame_rect_t result = {
        .x = x,
        .y = y,
        .width = (uint32_t)(maximum_x - x),
        .height = (uint32_t)(maximum_y - y),
    };
    return result;
}

static bool rect_merge_efficient(const display_frame_rect_t *left,
                                 const display_frame_rect_t *right,
                                 const display_frame_rect_t *combined) {
    uint64_t left_area = (uint64_t)left->width * left->height;
    uint64_t right_area = (uint64_t)right->width * right->height;
    uint64_t combined_area = (uint64_t)combined->width * combined->height;
    if (UINT64_MAX - left_area < right_area) return true;
    uint64_t separate_area = left_area + right_area;
    if (separate_area > UINT64_MAX / 2U) return true;
    return combined_area <= separate_area * 2U;
}

static void set_full_damage(display_frame_transaction_t *state) {
    state->damage[0] = (display_frame_rect_t){
        .x = 0U,
        .y = 0U,
        .width = state->screen_width,
        .height = state->screen_height,
    };
    state->damage_count = 1U;
}

static void copy_damage(const display_frame_transaction_t *state,
                        display_frame_rect_t *damage_out,
                        uint32_t *damage_count_out) {
    for (uint32_t index = 0U; index < state->damage_count; ++index)
        damage_out[index] = state->damage[index];
    *damage_count_out = state->damage_count;
}

static int prepare_transition(display_frame_transaction_t *state,
                              uint32_t action,
                              display_frame_rect_t *damage_out,
                              uint32_t *damage_count_out) {
    if (damage_out == NULL || damage_count_out == NULL) return FRAME_EINVAL;
    if (action != DISPLAY_FRAME_TRANSITION_PUBLISH &&
        action != DISPLAY_FRAME_TRANSITION_RESTORE) return FRAME_EINVAL;
    if (state->draw_active || state->transitioning) return FRAME_EBUSY;
    copy_damage(state, damage_out, damage_count_out);
    for (uint32_t index = 0U; index < state->damage_count; ++index)
        state->transition_damage[index] = state->damage[index];
    state->transition_damage_count = state->damage_count;
    state->transition_owner_pid = state->owner_pid;
    state->transition_owner_generation = state->owner_generation;
    state->transition_action = action;
    state->active = false;
    state->owner_pid = 0;
    state->owner_generation = 0U;
    state->serial = 0U;
    state->deadline_ms = 0U;
    state->damage_count = 0U;
    state->transitioning = true;
    return 0;
}

void display_frame_transaction_init(display_frame_transaction_t *state,
                                    uint32_t screen_width,
                                    uint32_t screen_height) {
    if (state == NULL) return;
    *state = (display_frame_transaction_t){0};
    state->screen_width = screen_width;
    state->screen_height = screen_height;
    state->next_serial = 1U;
}

int display_frame_begin(display_frame_transaction_t *state, int owner_pid,
                        uint32_t owner_generation, uint64_t now_ms,
                        uint32_t *serial_out) {
    if (state == NULL || serial_out == NULL ||
        !identity_valid(owner_pid, owner_generation) ||
        state->screen_width == 0U || state->screen_height == 0U)
        return FRAME_EINVAL;
    if (state->active || state->draw_active || state->transitioning)
        return FRAME_EBUSY;

    uint32_t serial = state->next_serial;
    if (serial == 0U) serial = 1U;
    state->next_serial = serial == UINT32_MAX ? 1U : serial + 1U;
    state->owner_pid = owner_pid;
    state->owner_generation = owner_generation;
    state->serial = serial;
    state->deadline_ms = now_ms > UINT64_MAX - DISPLAY_FRAME_LEASE_MS
        ? UINT64_MAX : now_ms + DISPLAY_FRAME_LEASE_MS;
    state->damage_count = 0U;
    state->active = true;
    *serial_out = serial;
    return 0;
}

int display_frame_draw_enter(display_frame_transaction_t *state, int pid,
                             uint32_t generation, uint64_t now_ms) {
    if (state == NULL || !identity_valid(pid, generation)) return FRAME_EINVAL;
    if (state->draw_active || state->transitioning) return FRAME_EBUSY;
    if (state->active) {
        if (!same_identity(pid, generation, state->owner_pid,
                           state->owner_generation)) return FRAME_EACCES;
        if (expired(state, now_ms)) return FRAME_ETIMEDOUT;
    }
    state->draw_pid = pid;
    state->draw_generation = generation;
    state->draw_active = true;
    return 0;
}

int display_frame_draw_leave(display_frame_transaction_t *state, int pid,
                             uint32_t generation, uint64_t now_ms) {
    if (state == NULL || !identity_valid(pid, generation)) return FRAME_EINVAL;
    if (!state->draw_active ||
        !same_identity(pid, generation, state->draw_pid,
                       state->draw_generation)) return FRAME_EACCES;
    state->draw_active = false;
    state->draw_pid = 0;
    state->draw_generation = 0U;
    return expired(state, now_ms) ? FRAME_ETIMEDOUT : 0;
}

bool display_frame_record_damage(display_frame_transaction_t *state,
                                 display_frame_rect_t rect) {
    /* draw_enter() is the user-facing ownership boundary.  Recording every
     * internal write while a frame is active also keeps kernel-originated
     * pixels hidden until that same frame is committed or rolled back. */
    if (state == NULL || !state->active) return false;
    if (rect.width == 0U || rect.height == 0U ||
        rect.x >= state->screen_width || rect.y >= state->screen_height)
        return true;
    if (rect.width > state->screen_width - rect.x)
        rect.width = state->screen_width - rect.x;
    if (rect.height > state->screen_height - rect.y)
        rect.height = state->screen_height - rect.y;

    for (uint32_t index = 0U; index < state->damage_count;) {
        if (!rect_intersects(&rect, &state->damage[index])) {
            ++index;
            continue;
        }
        display_frame_rect_t combined =
            rect_union(&rect, &state->damage[index]);
        if (!rect_merge_efficient(
                &rect, &state->damage[index], &combined)) {
            ++index;
            continue;
        }
        rect = combined;
        --state->damage_count;
        state->damage[index] = state->damage[state->damage_count];
        /* The union may now intersect an earlier rectangle. */
        index = 0U;
    }
    if (state->damage_count == DISPLAY_FRAME_DAMAGE_CAPACITY) {
        set_full_damage(state);
        return true;
    }
    state->damage[state->damage_count++] = rect;
    return true;
}

int display_frame_prepare_commit(display_frame_transaction_t *state, int pid,
                                 uint32_t generation, uint32_t serial,
                                 uint64_t now_ms,
                                 display_frame_rect_t *damage_out,
                                 uint32_t *damage_count_out) {
    if (state == NULL || !identity_valid(pid, generation)) return FRAME_EINVAL;
    if (!state->active || !same_identity(pid, generation, state->owner_pid,
                                         state->owner_generation))
        return FRAME_EACCES;
    if (serial == 0U || serial != state->serial) return FRAME_ESTALE;
    if (expired(state, now_ms)) return FRAME_ETIMEDOUT;
    return prepare_transition(state, DISPLAY_FRAME_TRANSITION_PUBLISH,
                              damage_out, damage_count_out);
}

int display_frame_prepare_cancel(display_frame_transaction_t *state, int pid,
                                 uint32_t generation, uint32_t serial,
                                 display_frame_rect_t *damage_out,
                                 uint32_t *damage_count_out) {
    if (state == NULL || !identity_valid(pid, generation)) return FRAME_EINVAL;
    if (!state->active || !same_identity(pid, generation, state->owner_pid,
                                         state->owner_generation))
        return FRAME_EACCES;
    if (serial == 0U || serial != state->serial) return FRAME_ESTALE;
    return prepare_transition(state, DISPLAY_FRAME_TRANSITION_RESTORE,
                              damage_out, damage_count_out);
}

int display_frame_prepare_expired(display_frame_transaction_t *state,
                                  uint64_t now_ms,
                                  display_frame_rect_t *damage_out,
                                  uint32_t *damage_count_out) {
    if (state == NULL) return FRAME_EINVAL;
    if (!expired(state, now_ms)) return 0;
    int result = prepare_transition(state, DISPLAY_FRAME_TRANSITION_RESTORE,
                                    damage_out, damage_count_out);
    return result == 0 ? 1 : result;
}

int display_frame_prepare_cleanup(display_frame_transaction_t *state, int pid,
                                  uint32_t generation,
                                  display_frame_rect_t *damage_out,
                                  uint32_t *damage_count_out) {
    if (state == NULL || !identity_valid(pid, generation)) return FRAME_EINVAL;
    if (state->draw_active &&
        same_identity(pid, generation, state->draw_pid,
                      state->draw_generation)) {
        state->draw_active = false;
        state->draw_pid = 0;
        state->draw_generation = 0U;
    }
    if (!state->active || !same_identity(pid, generation, state->owner_pid,
                                         state->owner_generation)) return 0;
    int result = prepare_transition(state, DISPLAY_FRAME_TRANSITION_RESTORE,
                                    damage_out, damage_count_out);
    return result == 0 ? 1 : result;
}

int display_frame_recover_transition(display_frame_transaction_t *state,
                                     int pid, uint32_t generation,
                                     display_frame_rect_t *damage_out,
                                     uint32_t *damage_count_out,
                                     uint32_t *action_out) {
    if (state == NULL || damage_out == NULL || damage_count_out == NULL ||
        action_out == NULL || !identity_valid(pid, generation))
        return FRAME_EINVAL;
    if (!state->transitioning ||
        !same_identity(pid, generation, state->transition_owner_pid,
                       state->transition_owner_generation)) return 0;
    if (state->transition_damage_count > DISPLAY_FRAME_DAMAGE_CAPACITY ||
        (state->transition_action != DISPLAY_FRAME_TRANSITION_PUBLISH &&
         state->transition_action != DISPLAY_FRAME_TRANSITION_RESTORE))
        return FRAME_EINVAL;
    for (uint32_t index = 0U; index < state->transition_damage_count; ++index)
        damage_out[index] = state->transition_damage[index];
    *damage_count_out = state->transition_damage_count;
    *action_out = state->transition_action;
    return 1;
}

void display_frame_finish_transition(display_frame_transaction_t *state) {
    if (state == NULL) return;
    state->transitioning = false;
    state->transition_damage_count = 0U;
    state->transition_owner_pid = 0;
    state->transition_owner_generation = 0U;
    state->transition_action = 0U;
}
