/**
 * @file userspace/gui/compositor/desktop_drag.h
 * @brief Fixed-capacity compositor-internal drag-and-drop controller.
 *
 * The controller separates a producer-scoped source, immutable object data,
 * target acceptance and the selected operation. Hovering is side-effect free;
 * a consumer performs an operation only after an accepted drop result.
 */
#ifndef USERSPACE_DESKTOP_DRAG_H
#define USERSPACE_DESKTOP_DRAG_H

#include <stdint.h>

#include "desktop_wm.h"

#define DESKTOP_DRAG_API_VERSION 1U
#define DESKTOP_DRAG_DATA_CAPACITY 576U
#define DESKTOP_DRAG_START_THRESHOLD 4U

#define DESKTOP_DRAG_KIND_MASK(kind) (1U << ((kind) - 1U))

enum desktop_drag_status {
    DESKTOP_DRAG_OK = 0,
    DESKTOP_DRAG_EINVAL = -22,
    DESKTOP_DRAG_ESTATE = -71
};

enum desktop_drag_object_kind {
    DESKTOP_DRAG_OBJECT_FILE = 1U,
    DESKTOP_DRAG_OBJECT_TEXT,
    DESKTOP_DRAG_OBJECT_IMAGE,
    DESKTOP_DRAG_OBJECT_APPLICATION,
    DESKTOP_DRAG_OBJECT_KIND_COUNT
};

enum desktop_drag_operation {
    DESKTOP_DRAG_OPERATION_MOVE = 1U << 0,
    DESKTOP_DRAG_OPERATION_COPY = 1U << 1,
    DESKTOP_DRAG_OPERATION_LINK = 1U << 2
};

#define DESKTOP_DRAG_OPERATION_ALL \
    (DESKTOP_DRAG_OPERATION_MOVE | DESKTOP_DRAG_OPERATION_COPY | \
     DESKTOP_DRAG_OPERATION_LINK)

enum desktop_drag_phase {
    DESKTOP_DRAG_PHASE_IDLE = 0U,
    DESKTOP_DRAG_PHASE_ARMED,
    DESKTOP_DRAG_PHASE_DRAGGING
};

enum desktop_drag_feedback {
    DESKTOP_DRAG_FEEDBACK_NONE = 0U,
    DESKTOP_DRAG_FEEDBACK_INVALID,
    DESKTOP_DRAG_FEEDBACK_VALID
};

typedef struct desktop_drag_object {
    uint32_t version;
    uint32_t struct_size;
    uint32_t kind;
    uint32_t operations;
    uint32_t source_id;
    uint32_t source_generation;
    uint32_t data_size;
    uint32_t reserved[3];
    uint8_t data[DESKTOP_DRAG_DATA_CAPACITY];
} desktop_drag_object_t;

typedef struct desktop_drag_target {
    uint32_t version;
    uint32_t struct_size;
    desktop_rect_t bounds;
    uint32_t accepted_kinds;
    uint32_t operations;
    uint32_t target_id;
    uint32_t target_generation;
    uint32_t reserved[4];
} desktop_drag_target_t;

typedef struct desktop_drag_state {
    uint32_t phase;
    uint32_t feedback;
    int32_t start_x;
    int32_t start_y;
    int32_t current_x;
    int32_t current_y;
    uint32_t target_id;
    uint32_t target_generation;
    uint32_t operation;
    desktop_drag_object_t object;
} desktop_drag_state_t;

typedef struct desktop_drag_result {
    uint32_t accepted;
    uint32_t operation;
    uint32_t target_id;
    uint32_t target_generation;
    desktop_drag_object_t object;
} desktop_drag_result_t;

void desktop_drag_object_initialize(desktop_drag_object_t *object);
void desktop_drag_target_initialize(desktop_drag_target_t *target);
void desktop_drag_state_initialize(desktop_drag_state_t *state);
void desktop_drag_result_initialize(desktop_drag_result_t *result);
int desktop_drag_validate_object(const desktop_drag_object_t *object);
int desktop_drag_validate_target(const desktop_drag_target_t *target);
int desktop_drag_arm(desktop_drag_state_t *state,
                     const desktop_drag_object_t *object,
                     int32_t x, int32_t y);
int desktop_drag_motion(desktop_drag_state_t *state, int32_t x, int32_t y,
                        const desktop_drag_target_t *target,
                        uint32_t requested_operation);
int desktop_drag_drop(desktop_drag_state_t *state, int32_t x, int32_t y,
                      const desktop_drag_target_t *target,
                      uint32_t requested_operation,
                      desktop_drag_result_t *result);
void desktop_drag_cancel(desktop_drag_state_t *state);

#endif
