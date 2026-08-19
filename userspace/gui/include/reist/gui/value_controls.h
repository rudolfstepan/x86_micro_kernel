/**
 * @file reist/gui/value_controls.h
 * @brief Bounded text, list and numeric value controls for Ring-3 clients.
 *
 * These controllers are renderer-independent and heap-free. Models and item
 * arrays are immutable caller-owned data; mutable state is caller-owned and
 * fixed capacity. Every visible control carries a stable nonzero id and a
 * nonempty semantic name. Coordinates are local half-open rectangles.
 *
 * Version 1 intentionally accepts printable ASCII text only. Clipboard,
 * UTF-8 grapheme editing and IME composition require separate versioned
 * service contracts and are not silently approximated.
 */
#ifndef REIST_GUI_VALUE_CONTROLS_H
#define REIST_GUI_VALUE_CONTROLS_H

#include <stddef.h>
#include <stdint.h>

#include "reist/gui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_VALUE_API_VERSION 1U
#define REIST_GUI_VALUE_NAME_LIMIT 48U
#define REIST_GUI_TEXT_CAPACITY 64U
#define REIST_GUI_LIST_CAPACITY 32U
#define REIST_GUI_VALUE_DAMAGE_CAPACITY 4U
#define REIST_GUI_VALUE_NO_ID 0U
#define REIST_GUI_VALUE_NO_INDEX UINT32_MAX

enum reist_gui_value_status {
    REIST_GUI_VALUE_OK = 0,
    REIST_GUI_VALUE_EINVAL = -1,
    REIST_GUI_VALUE_ECAPACITY = -2
};

enum reist_gui_value_flags {
    REIST_GUI_VALUE_VISIBLE = 1U << 0,
    REIST_GUI_VALUE_ENABLED = 1U << 1,
    REIST_GUI_VALUE_READ_ONLY = 1U << 2
};

enum reist_gui_value_event_type {
    REIST_GUI_VALUE_EVENT_POINTER_MOTION = 1U,
    REIST_GUI_VALUE_EVENT_POINTER_BUTTON,
    REIST_GUI_VALUE_EVENT_KEYBOARD,
    REIST_GUI_VALUE_EVENT_TEXT,
    REIST_GUI_VALUE_EVENT_FOCUS,
    REIST_GUI_VALUE_EVENT_CANCEL
};

enum reist_gui_value_key {
    REIST_GUI_VALUE_KEY_LEFT = 1U,
    REIST_GUI_VALUE_KEY_RIGHT,
    REIST_GUI_VALUE_KEY_UP,
    REIST_GUI_VALUE_KEY_DOWN,
    REIST_GUI_VALUE_KEY_HOME,
    REIST_GUI_VALUE_KEY_END,
    REIST_GUI_VALUE_KEY_PAGE_UP,
    REIST_GUI_VALUE_KEY_PAGE_DOWN,
    REIST_GUI_VALUE_KEY_BACKSPACE,
    REIST_GUI_VALUE_KEY_DELETE,
    REIST_GUI_VALUE_KEY_ENTER
};

enum reist_gui_value_button {
    REIST_GUI_VALUE_BUTTON_LEFT = 1U
};

enum reist_gui_range_role {
    REIST_GUI_RANGE_SLIDER = 1U,
    REIST_GUI_RANGE_SCROLLBAR,
    REIST_GUI_RANGE_SPIN_BOX,
    REIST_GUI_RANGE_PROGRESS
};

enum reist_gui_orientation {
    REIST_GUI_HORIZONTAL = 1U,
    REIST_GUI_VERTICAL
};

typedef struct reist_gui_value_event {
    uint32_t version;
    uint32_t struct_size;
    uint32_t type;
    int32_t x;
    int32_t y;
    uint32_t button;
    uint32_t pressed;
    uint32_t key;
    uint32_t codepoint; /**< Printable ASCII for TEXT; zero otherwise. */
    uint32_t focused;   /**< Zero/one for FOCUS; zero otherwise. */
    uint32_t reserved[4];
} reist_gui_value_event_t;

typedef struct reist_gui_value_result {
    uint32_t version;
    uint32_t struct_size;
    uint32_t consumed;
    uint32_t changed;
    uint32_t activated;
    uint32_t focus_changed;
    uint32_t control_id;
    uint32_t selected_id;
    int32_t value;
    uint32_t cursor;
    uint32_t damage_count;
    uint32_t full_redraw;
    reist_gui_rect_t damage[REIST_GUI_VALUE_DAMAGE_CAPACITY];
    uint32_t reserved[4];
} reist_gui_value_result_t;

typedef struct reist_gui_text_model {
    uint32_t version;
    uint32_t struct_size;
    uint32_t id;
    const char *name;
    reist_gui_rect_t bounds;
    uint32_t capacity;    /**< 2..REIST_GUI_TEXT_CAPACITY including NUL. */
    uint32_t glyph_width; /**< Pixel width used for pointer-to-cursor mapping. */
    uint32_t flags;
    uint32_t reserved[4];
} reist_gui_text_model_t;

typedef struct reist_gui_text_state {
    uint32_t version;
    uint32_t struct_size;
    uint32_t configured;
    uint32_t focused;
    uint32_t captured;
    uint32_t length;
    uint32_t cursor;
    char text[REIST_GUI_TEXT_CAPACITY];
    uint32_t reserved[4];
} reist_gui_text_state_t;

typedef struct reist_gui_list_item {
    uint32_t id;
    const char *label;
    uint32_t flags;
    uint32_t reserved[2];
} reist_gui_list_item_t;

typedef struct reist_gui_list_model {
    uint32_t version;
    uint32_t struct_size;
    uint32_t id;
    const char *name;
    const reist_gui_list_item_t *items;
    uint32_t item_count;
    reist_gui_rect_t bounds;
    uint32_t row_height;
    uint32_t flags;
    uint32_t reserved[4];
} reist_gui_list_model_t;

typedef struct reist_gui_list_state {
    uint32_t version;
    uint32_t struct_size;
    uint32_t configured;
    uint32_t focused;
    uint32_t selected;
    uint32_t top_index;
    uint32_t captured;
    uint32_t armed;
    uint32_t reserved[4];
} reist_gui_list_state_t;

typedef struct reist_gui_range_model {
    uint32_t version;
    uint32_t struct_size;
    uint32_t id;
    const char *name;
    reist_gui_rect_t bounds;
    int32_t minimum;
    int32_t maximum;
    uint32_t step;
    uint32_t page_step;
    uint32_t role;
    uint32_t orientation;
    uint32_t flags;
    uint32_t reserved[4];
} reist_gui_range_model_t;

typedef struct reist_gui_range_state {
    uint32_t version;
    uint32_t struct_size;
    uint32_t configured;
    uint32_t focused;
    uint32_t captured;
    int32_t value;
    uint32_t reserved[4];
} reist_gui_range_state_t;

/** Initialize a normalized event. @param[out] event NULL-safe destination. */
void reist_gui_value_event_initialize(reist_gui_value_event_t *event);
/** Initialize a bounded result. @param[out] result NULL-safe destination. */
void reist_gui_value_result_initialize(reist_gui_value_result_t *result);

/** Initialize text state. @param[out] state NULL-safe destination. */
void reist_gui_text_state_initialize(reist_gui_text_state_t *state);
/** Validate a text model/state pair. @param[in] model Immutable model.
 * @param[in] state Initialized/configured state. @return OK or EINVAL. */
int reist_gui_text_validate(const reist_gui_text_model_t *model,
                            const reist_gui_text_state_t *state);
/** Configure initial ASCII text. @param[in] model Immutable model.
 * @param[in,out] state Initialized state. @param[in] initial_text NUL text.
 * @param[in,out] result Initialized result. @return OK or an error before mutation. */
int reist_gui_text_configure(const reist_gui_text_model_t *model,
                             reist_gui_text_state_t *state,
                             const char *initial_text,
                             reist_gui_value_result_t *result);
/** Dispatch one text event. @param[in] model Configuration model.
 * @param[in,out] state Configured state. @param[in] event Normalized input.
 * @param[in,out] result Initialized result. @return OK or EINVAL. */
int reist_gui_text_dispatch(const reist_gui_text_model_t *model,
                            reist_gui_text_state_t *state,
                            const reist_gui_value_event_t *event,
                            reist_gui_value_result_t *result);

/** Initialize list state. @param[out] state NULL-safe destination. */
void reist_gui_list_state_initialize(reist_gui_list_state_t *state);
/** Validate a list model/state pair. @param[in] model Immutable model.
 * @param[in] state Initialized/configured state. @return OK or EINVAL. */
int reist_gui_list_validate(const reist_gui_list_model_t *model,
                            const reist_gui_list_state_t *state);
/** Configure one selected item. @param[in] model Immutable model.
 * @param[in,out] state Initialized state. @param[in] initial_item_id ID or zero.
 * @param[in,out] result Initialized result. @return OK or an error. */
int reist_gui_list_configure(const reist_gui_list_model_t *model,
                             reist_gui_list_state_t *state,
                             uint32_t initial_item_id,
                             reist_gui_value_result_t *result);
/** Dispatch one list event. @param[in] model Configuration model.
 * @param[in,out] state Configured state. @param[in] event Normalized input.
 * @param[in,out] result Initialized result. @return OK or EINVAL. */
int reist_gui_list_dispatch(const reist_gui_list_model_t *model,
                            reist_gui_list_state_t *state,
                            const reist_gui_value_event_t *event,
                            reist_gui_value_result_t *result);

/** Initialize range state. @param[out] state NULL-safe destination. */
void reist_gui_range_state_initialize(reist_gui_range_state_t *state);
/** Validate a range model/state pair. @param[in] model Immutable model.
 * @param[in] state Initialized/configured state. @return OK or EINVAL. */
int reist_gui_range_validate(const reist_gui_range_model_t *model,
                             const reist_gui_range_state_t *state);
/** Configure an initial value. @param[in] model Immutable range model.
 * @param[in,out] state Initialized state. @param[in] initial_value In range.
 * @param[in,out] result Initialized result. @return OK or EINVAL. */
int reist_gui_range_configure(const reist_gui_range_model_t *model,
                              reist_gui_range_state_t *state,
                              int32_t initial_value,
                              reist_gui_value_result_t *result);
/** Set a value programmatically. @param[in] model Configuration model.
 * @param[in,out] state Configured state. @param[in] value In-range value.
 * @param[in,out] result Initialized result. @return OK or EINVAL. */
int reist_gui_range_set(const reist_gui_range_model_t *model,
                        reist_gui_range_state_t *state, int32_t value,
                        reist_gui_value_result_t *result);
/** Dispatch one interactive range event. @param[in] model Configuration model.
 * @param[in,out] state Configured state. @param[in] event Normalized input.
 * @param[in,out] result Initialized result. @return OK or EINVAL. */
int reist_gui_range_dispatch(const reist_gui_range_model_t *model,
                             reist_gui_range_state_t *state,
                             const reist_gui_value_event_t *event,
                             reist_gui_value_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_VALUE_CONTROLS_H */
