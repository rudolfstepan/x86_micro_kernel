/**
 * @file reist/gui/tabs.h
 * @brief Fixed-capacity renderer-independent tab sheet controller.
 *
 * A tab sheet owns no pixels and allocates no memory. The application keeps
 * the immutable tab model and the mutable state alive for every call. Tab and
 * content rectangles use caller-local half-open coordinates. Exactly one
 * visible, enabled page is selected after configuration.
 *
 * Pointer down on a tab establishes an implicit capture until pointer up or
 * cancel. Horizontal keyboard navigation follows the W3C Tabs pattern:
 * Left/Right wrap, Home/End select an edge, and Space/Enter activate the
 * focused tab. Arrow focus automatically selects because page switching is
 * local and immediate. Tab itself remains part of the application's outer
 * focus traversal and is intentionally not consumed here.
 */
#ifndef REIST_GUI_TABS_H
#define REIST_GUI_TABS_H

#include <stddef.h>
#include <stdint.h>

#include "reist/gui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_TABS_API_VERSION 1U
#define REIST_GUI_TABS_CAPACITY 8U
#define REIST_GUI_TABS_DAMAGE_CAPACITY 4U
#define REIST_GUI_TABS_LABEL_LIMIT 48U
#define REIST_GUI_TABS_NO_ID 0U
#define REIST_GUI_TABS_NO_INDEX UINT32_MAX

enum reist_gui_tabs_status {
    REIST_GUI_TABS_OK = 0,
    REIST_GUI_TABS_EINVAL = -1,
    REIST_GUI_TABS_ECAPACITY = -2
};

enum reist_gui_tab_flags {
    REIST_GUI_TAB_VISIBLE = 1U << 0,
    REIST_GUI_TAB_ENABLED = 1U << 1
};

enum reist_gui_tabs_event_type {
    REIST_GUI_TABS_EVENT_POINTER_MOTION = 1U,
    REIST_GUI_TABS_EVENT_POINTER_BUTTON,
    REIST_GUI_TABS_EVENT_KEYBOARD,
    REIST_GUI_TABS_EVENT_CANCEL
};

enum reist_gui_tabs_key {
    REIST_GUI_TABS_KEY_LEFT = 1U,
    REIST_GUI_TABS_KEY_RIGHT,
    REIST_GUI_TABS_KEY_HOME,
    REIST_GUI_TABS_KEY_END,
    REIST_GUI_TABS_KEY_ENTER,
    REIST_GUI_TABS_KEY_SPACE
};

enum reist_gui_tabs_button {
    REIST_GUI_TABS_BUTTON_LEFT = 1U
};

/** Immutable descriptor of one labelled page selector. */
typedef struct reist_gui_tab {
    uint32_t id;       /**< Stable nonzero tab identity. */
    uint32_t page_id;  /**< Stable nonzero application page identity. */
    const char *label; /**< Caller-owned accessible label, NUL within limit. */
    uint32_t width;    /**< Pixel width in the horizontal tab bar. */
    uint32_t flags;    /**< Bitset from reist_gui_tab_flags. */
    uint32_t reserved[3]; /**< Must be zero. */
} reist_gui_tab_t;

/** Immutable tab sheet model. */
typedef struct reist_gui_tabs_model {
    uint32_t version;
    uint32_t struct_size;
    const reist_gui_tab_t *tabs; /**< Caller-owned array. */
    uint32_t tab_count;          /**< One through REIST_GUI_TABS_CAPACITY. */
    reist_gui_rect_t tab_bar;    /**< Horizontal strip containing all tabs. */
    reist_gui_rect_t content;    /**< Selected page area below the strip. */
    uint32_t damage_margin;
    uint32_t reserved[4]; /**< Must be zero. */
} reist_gui_tabs_model_t;

/** Mutable caller-owned tab sheet state. */
typedef struct reist_gui_tabs_state {
    uint32_t version;
    uint32_t struct_size;
    uint32_t configured;
    uint32_t tab_count;
    uint32_t selected;
    uint32_t focused;
    uint32_t hovered;
    uint32_t captured;
    uint32_t armed;
    uint32_t reserved[4]; /**< Must be zero. */
} reist_gui_tabs_state_t;

/** One normalized local input event. */
typedef struct reist_gui_tabs_event {
    uint32_t version;
    uint32_t struct_size;
    uint32_t type;
    int32_t x;
    int32_t y;
    uint32_t button;
    uint32_t pressed;
    uint32_t key;
    uint32_t reserved[4]; /**< Must be zero. */
} reist_gui_tabs_event_t;

/** Bounded output from one operation; initialize before every call. */
typedef struct reist_gui_tabs_result {
    uint32_t version;
    uint32_t struct_size;
    uint32_t consumed;
    uint32_t selection_changed;
    uint32_t focus_changed;
    uint32_t selected_id;
    uint32_t page_id;
    uint32_t focused_id;
    uint32_t damage_count;
    uint32_t full_redraw;
    reist_gui_rect_t damage[REIST_GUI_TABS_DAMAGE_CAPACITY];
    uint32_t reserved[4]; /**< Must be zero. */
} reist_gui_tabs_result_t;

/** Initialize state. @param[out] state NULL-safe destination. */
void reist_gui_tabs_state_initialize(reist_gui_tabs_state_t *state);
/** Initialize an event. @param[out] event NULL-safe destination. */
void reist_gui_tabs_event_initialize(reist_gui_tabs_event_t *event);
/** Initialize a result. @param[out] result NULL-safe destination. */
void reist_gui_tabs_result_initialize(reist_gui_tabs_result_t *result);

/** Validate model and state. @param[in] model Immutable model.
 * @param[in] state Initialized/configured state. @return OK or an error. */
int reist_gui_tabs_validate(const reist_gui_tabs_model_t *model,
                            const reist_gui_tabs_state_t *state);
/** Configure one initial page. @param[in] model Immutable model.
 * @param[in,out] state Initialized state. @param[in] initial_tab_id ID or zero.
 * @param[in,out] result Initialized result. @return OK or an error before mutation. */
int reist_gui_tabs_configure(const reist_gui_tabs_model_t *model,
                             reist_gui_tabs_state_t *state,
                             uint32_t initial_tab_id,
                             reist_gui_tabs_result_t *result);
/** Dispatch one normalized input. @param[in] model Configuration model.
 * @param[in,out] state Configured state. @param[in] event Input event.
 * @param[in,out] result Initialized bounded result. @return OK or EINVAL. */
int reist_gui_tabs_dispatch(const reist_gui_tabs_model_t *model,
                            reist_gui_tabs_state_t *state,
                            const reist_gui_tabs_event_t *event,
                            reist_gui_tabs_result_t *result);
/** Select a page programmatically. @param[in] model Configuration model.
 * @param[in,out] state Configured state. @param[in] tab_id Enabled tab ID.
 * @param[in,out] result Initialized result. @return OK or EINVAL. */
int reist_gui_tabs_select(const reist_gui_tabs_model_t *model,
                          reist_gui_tabs_state_t *state,
                          uint32_t tab_id,
                          reist_gui_tabs_result_t *result);
/** Query tab geometry. @param[in] model Valid model. @param[in] index Index.
 * @param[out] rect_out Local rectangle. @return OK or an error. */
int reist_gui_tabs_tab_rect(const reist_gui_tabs_model_t *model,
                            uint32_t index, reist_gui_rect_t *rect_out);
/** Query selected IDs. @param[in] model Configuration model.
 * @param[in] state Configured state. @param[out] tab_id_out Tab ID.
 * @param[out] page_id_out Page ID. @return OK or EINVAL. */
int reist_gui_tabs_selected(const reist_gui_tabs_model_t *model,
                            const reist_gui_tabs_state_t *state,
                            uint32_t *tab_id_out, uint32_t *page_id_out);

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_TABS_H */
