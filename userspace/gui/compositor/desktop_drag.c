/**
 * @file userspace/gui/compositor/desktop_drag.c
 * @brief Heap-free drag source/object/target/operation state machine.
 */
#include "desktop_drag.h"

#include <limits.h>

static void clear_bytes(void *value, uint32_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)value;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static void copy_object(desktop_drag_object_t *destination,
                        const desktop_drag_object_t *source) {
    volatile uint8_t *output = (volatile uint8_t *)destination;
    const volatile uint8_t *input = (const volatile uint8_t *)source;
    for (uint32_t index = 0U; index < sizeof(*destination); ++index)
        output[index] = input[index];
}

static uint32_t reserved_zero(const uint32_t *values, uint32_t count) {
    for (uint32_t index = 0U; index < count; ++index)
        if (values[index] != 0U) return 0U;
    return 1U;
}

static uint32_t point_in_rect(desktop_rect_t rect, int32_t x, int32_t y) {
    int64_t right = (int64_t)rect.x + rect.width;
    int64_t bottom = (int64_t)rect.y + rect.height;
    return rect.width != 0U && rect.height != 0U && x >= rect.x && y >= rect.y &&
        (int64_t)x < right && (int64_t)y < bottom;
}

static uint32_t single_operation(uint32_t operation) {
    return operation != 0U &&
        (operation & (operation - 1U)) == 0U &&
        (operation & ~DESKTOP_DRAG_OPERATION_ALL) == 0U;
}

static uint32_t choose_operation(uint32_t available, uint32_t requested) {
    if (requested != 0U)
        return single_operation(requested) && (available & requested) != 0U
            ? requested : 0U;
    if ((available & DESKTOP_DRAG_OPERATION_MOVE) != 0U)
        return DESKTOP_DRAG_OPERATION_MOVE;
    if ((available & DESKTOP_DRAG_OPERATION_COPY) != 0U)
        return DESKTOP_DRAG_OPERATION_COPY;
    if ((available & DESKTOP_DRAG_OPERATION_LINK) != 0U)
        return DESKTOP_DRAG_OPERATION_LINK;
    return 0U;
}

void desktop_drag_object_initialize(desktop_drag_object_t *object) {
    if (object == 0) return;
    clear_bytes(object, sizeof(*object));
    object->version = DESKTOP_DRAG_API_VERSION;
    object->struct_size = sizeof(*object);
}

void desktop_drag_target_initialize(desktop_drag_target_t *target) {
    if (target == 0) return;
    clear_bytes(target, sizeof(*target));
    target->version = DESKTOP_DRAG_API_VERSION;
    target->struct_size = sizeof(*target);
}

void desktop_drag_state_initialize(desktop_drag_state_t *state) {
    if (state == 0) return;
    clear_bytes(state, sizeof(*state));
    state->phase = DESKTOP_DRAG_PHASE_IDLE;
}

void desktop_drag_result_initialize(desktop_drag_result_t *result) {
    if (result == 0) return;
    clear_bytes(result, sizeof(*result));
    desktop_drag_object_initialize(&result->object);
}

int desktop_drag_validate_object(const desktop_drag_object_t *object) {
    if (object == 0 || object->version != DESKTOP_DRAG_API_VERSION ||
        object->struct_size < sizeof(*object) ||
        object->kind < DESKTOP_DRAG_OBJECT_FILE ||
        object->kind >= DESKTOP_DRAG_OBJECT_KIND_COUNT ||
        object->operations == 0U ||
        (object->operations & ~DESKTOP_DRAG_OPERATION_ALL) != 0U ||
        object->source_generation == 0U ||
        object->data_size > DESKTOP_DRAG_DATA_CAPACITY ||
        !reserved_zero(object->reserved, 3U)) return DESKTOP_DRAG_EINVAL;
    return DESKTOP_DRAG_OK;
}

int desktop_drag_validate_target(const desktop_drag_target_t *target) {
    if (target == 0 || target->version != DESKTOP_DRAG_API_VERSION ||
        target->struct_size < sizeof(*target) ||
        target->bounds.width == 0U || target->bounds.height == 0U ||
        (int64_t)target->bounds.x + target->bounds.width > INT32_MAX ||
        (int64_t)target->bounds.y + target->bounds.height > INT32_MAX ||
        target->accepted_kinds == 0U ||
        (target->accepted_kinds & ~
            ((1U << (DESKTOP_DRAG_OBJECT_KIND_COUNT - 1U)) - 1U)) != 0U ||
        target->operations == 0U ||
        (target->operations & ~DESKTOP_DRAG_OPERATION_ALL) != 0U ||
        target->target_generation == 0U ||
        !reserved_zero(target->reserved, 4U)) return DESKTOP_DRAG_EINVAL;
    return DESKTOP_DRAG_OK;
}

int desktop_drag_arm(desktop_drag_state_t *state,
                     const desktop_drag_object_t *object,
                     int32_t x, int32_t y) {
    if (state == 0 || desktop_drag_validate_object(object) != DESKTOP_DRAG_OK)
        return DESKTOP_DRAG_EINVAL;
    if (state->phase != DESKTOP_DRAG_PHASE_IDLE) return DESKTOP_DRAG_ESTATE;
    state->phase = DESKTOP_DRAG_PHASE_ARMED;
    state->feedback = DESKTOP_DRAG_FEEDBACK_NONE;
    state->start_x = state->current_x = x;
    state->start_y = state->current_y = y;
    state->target_id = 0U;
    state->target_generation = 0U;
    state->operation = 0U;
    copy_object(&state->object, object);
    return DESKTOP_DRAG_OK;
}

int desktop_drag_motion(desktop_drag_state_t *state, int32_t x, int32_t y,
                        const desktop_drag_target_t *target,
                        uint32_t requested_operation) {
    if (state == 0 || state->phase == DESKTOP_DRAG_PHASE_IDLE ||
        desktop_drag_validate_object(&state->object) != DESKTOP_DRAG_OK)
        return DESKTOP_DRAG_ESTATE;
    state->current_x = x;
    state->current_y = y;
    if (state->phase == DESKTOP_DRAG_PHASE_ARMED) {
        int64_t delta_x = (int64_t)x - state->start_x;
        int64_t delta_y = (int64_t)y - state->start_y;
        if (delta_x < 0) delta_x = -delta_x;
        if (delta_y < 0) delta_y = -delta_y;
        if (delta_x < DESKTOP_DRAG_START_THRESHOLD &&
            delta_y < DESKTOP_DRAG_START_THRESHOLD) return DESKTOP_DRAG_OK;
        state->phase = DESKTOP_DRAG_PHASE_DRAGGING;
    }
    state->feedback = DESKTOP_DRAG_FEEDBACK_INVALID;
    state->target_id = 0U;
    state->target_generation = 0U;
    state->operation = 0U;
    if (target == 0 ||
        desktop_drag_validate_target(target) != DESKTOP_DRAG_OK ||
        !point_in_rect(target->bounds, x, y) ||
        (target->accepted_kinds &
            DESKTOP_DRAG_KIND_MASK(state->object.kind)) == 0U)
        return DESKTOP_DRAG_OK;
    uint32_t operation = choose_operation(
        state->object.operations & target->operations, requested_operation);
    if (operation == 0U) return DESKTOP_DRAG_OK;
    state->feedback = DESKTOP_DRAG_FEEDBACK_VALID;
    state->target_id = target->target_id;
    state->target_generation = target->target_generation;
    state->operation = operation;
    return DESKTOP_DRAG_OK;
}

int desktop_drag_drop(desktop_drag_state_t *state, int32_t x, int32_t y,
                      const desktop_drag_target_t *target,
                      uint32_t requested_operation,
                      desktop_drag_result_t *result) {
    if (state == 0 || result == 0 || state->phase == DESKTOP_DRAG_PHASE_IDLE)
        return DESKTOP_DRAG_ESTATE;
    desktop_drag_result_initialize(result);
    if (state->phase == DESKTOP_DRAG_PHASE_DRAGGING)
        (void)desktop_drag_motion(
            state, x, y, target, requested_operation);
    if (state->phase == DESKTOP_DRAG_PHASE_DRAGGING &&
        state->feedback == DESKTOP_DRAG_FEEDBACK_VALID) {
        result->accepted = 1U;
        result->operation = state->operation;
        result->target_id = state->target_id;
        result->target_generation = state->target_generation;
        copy_object(&result->object, &state->object);
    }
    desktop_drag_state_initialize(state);
    return DESKTOP_DRAG_OK;
}

void desktop_drag_cancel(desktop_drag_state_t *state) {
    desktop_drag_state_initialize(state);
}
