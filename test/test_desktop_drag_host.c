#include <assert.h>
#include <stdint.h>

#include "userspace/gui/compositor/desktop_drag.h"

int main(void) {
    desktop_drag_object_t object;
    desktop_drag_object_initialize(&object);
    object.kind = DESKTOP_DRAG_OBJECT_FILE;
    object.operations = DESKTOP_DRAG_OPERATION_MOVE |
        DESKTOP_DRAG_OPERATION_COPY | DESKTOP_DRAG_OPERATION_LINK |
        DESKTOP_DRAG_OPERATION_LAYOUT;
    object.source_id = 3U;
    object.source_generation = 9U;
    object.data_size = 3U;
    object.data[0] = 'a';
    object.data[1] = 'b';
    object.data[2] = 'c';
    assert(desktop_drag_validate_object(&object) == DESKTOP_DRAG_OK);

    desktop_drag_state_t state;
    desktop_drag_result_t result;

    desktop_drag_target_t layout_target;
    desktop_drag_target_initialize(&layout_target);
    layout_target.bounds = (desktop_rect_t){0, 0, 80U, 80U};
    layout_target.accepted_kinds = DESKTOP_DRAG_KIND_MASK(
        DESKTOP_DRAG_OBJECT_FILE);
    layout_target.operations = DESKTOP_DRAG_OPERATION_LAYOUT;
    layout_target.target_id = 19U;
    layout_target.target_generation = 12U;
    assert(desktop_drag_validate_target(&layout_target) == DESKTOP_DRAG_OK);

    desktop_drag_state_initialize(&state);
    assert(desktop_drag_arm(&state, &object, 4, 4) == DESKTOP_DRAG_OK);
    assert(desktop_drag_motion(&state, 30, 30, &layout_target,
                               DESKTOP_DRAG_OPERATION_LAYOUT) ==
           DESKTOP_DRAG_OK);
    desktop_drag_result_initialize(&result);
    assert(desktop_drag_drop(&state, 30, 30, &layout_target,
                             DESKTOP_DRAG_OPERATION_LAYOUT, &result) ==
           DESKTOP_DRAG_OK);
    assert(result.accepted == 1U);
    assert(result.operation == DESKTOP_DRAG_OPERATION_LAYOUT);

    desktop_drag_target_t target;
    desktop_drag_target_initialize(&target);
    target.bounds = (desktop_rect_t){100, 100, 50U, 50U};
    target.accepted_kinds = DESKTOP_DRAG_KIND_MASK(
        DESKTOP_DRAG_OBJECT_FILE);
    target.operations = DESKTOP_DRAG_OPERATION_MOVE;
    target.target_id = 7U;
    target.target_generation = 11U;
    assert(desktop_drag_validate_target(&target) == DESKTOP_DRAG_OK);

    desktop_drag_state_initialize(&state);
    assert(desktop_drag_arm(&state, &object, 10, 10) == DESKTOP_DRAG_OK);
    assert(state.phase == DESKTOP_DRAG_PHASE_ARMED);
    assert(desktop_drag_motion(&state, 12, 12, 0, 0U) == DESKTOP_DRAG_OK);
    assert(state.phase == DESKTOP_DRAG_PHASE_ARMED);
    assert(desktop_drag_motion(&state, 30, 30, 0, 0U) == DESKTOP_DRAG_OK);
    assert(state.phase == DESKTOP_DRAG_PHASE_DRAGGING);
    assert(state.feedback == DESKTOP_DRAG_FEEDBACK_INVALID);
    assert(desktop_drag_motion(
               &state, 110, 110, &target,
               DESKTOP_DRAG_OPERATION_MOVE) == DESKTOP_DRAG_OK);
    assert(state.feedback == DESKTOP_DRAG_FEEDBACK_VALID);

    desktop_drag_result_initialize(&result);
    assert(desktop_drag_drop(
               &state, 110, 110, &target,
               DESKTOP_DRAG_OPERATION_MOVE, &result) == DESKTOP_DRAG_OK);
    assert(result.accepted == 1U);
    assert(result.operation == DESKTOP_DRAG_OPERATION_MOVE);
    assert(result.target_id == 7U && result.target_generation == 11U);
    assert(result.object.source_id == 3U);
    assert(result.object.data_size == 3U && result.object.data[2] == 'c');
    assert(state.phase == DESKTOP_DRAG_PHASE_IDLE);

    desktop_drag_state_initialize(&state);
    assert(desktop_drag_arm(&state, &object, 0, 0) == DESKTOP_DRAG_OK);
    assert(desktop_drag_motion(&state, 110, 110, &target,
                               DESKTOP_DRAG_OPERATION_COPY) ==
           DESKTOP_DRAG_OK);
    assert(state.feedback == DESKTOP_DRAG_FEEDBACK_INVALID);
    desktop_drag_result_initialize(&result);
    assert(desktop_drag_drop(&state, 110, 110, &target,
                             DESKTOP_DRAG_OPERATION_COPY, &result) ==
           DESKTOP_DRAG_OK);
    assert(result.accepted == 0U);

    object.data_size = DESKTOP_DRAG_DATA_CAPACITY + 1U;
    assert(desktop_drag_validate_object(&object) == DESKTOP_DRAG_EINVAL);
    return 0;
}
