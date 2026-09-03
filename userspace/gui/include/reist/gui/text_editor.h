/**
 * @file reist/gui/text_editor.h
 * @brief Fixed-capacity multiline text editor controller for Ring-3 clients.
 *
 * The controller owns no window, renderer, file descriptor, allocator or
 * event loop. The caller owns the immutable model and mutable state, routes
 * normalized input, paints returned damage and performs persistence. Text is
 * validated RFC 3629 UTF-8 with canonical LF line breaks. Cursor and viewport
 * columns count Unicode scalars while line and document capacities remain byte
 * capacities. Clipboard, selection, grapheme-aware editing, bidirectional
 * layout, shaping, IME and undo require later versioned contracts.
 *
 * Coordinates are caller-surface-local half-open pixels. Calls sharing one
 * state must be serialized. Every operation is bounded by the fixed line and
 * column capacities below.
 */
#ifndef REIST_GUI_TEXT_EDITOR_H
#define REIST_GUI_TEXT_EDITOR_H

#include <stddef.h>
#include <stdint.h>

#include "reist/gui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_TEXT_EDITOR_API_VERSION 1U
#define REIST_GUI_TEXT_EDITOR_MAX_LINES 200U
#define REIST_GUI_TEXT_EDITOR_LINE_CAPACITY 256U
#define REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY 51200U
#define REIST_GUI_TEXT_EDITOR_NAME_LIMIT 48U
#define REIST_GUI_TEXT_EDITOR_DAMAGE_CAPACITY 4U

enum reist_gui_text_editor_status {
    REIST_GUI_TEXT_EDITOR_OK = 0,
    REIST_GUI_TEXT_EDITOR_EINVAL = -1,
    REIST_GUI_TEXT_EDITOR_ECAPACITY = -2
};

enum reist_gui_text_editor_flags {
    REIST_GUI_TEXT_EDITOR_VISIBLE = 1U << 0,
    REIST_GUI_TEXT_EDITOR_ENABLED = 1U << 1,
    REIST_GUI_TEXT_EDITOR_READ_ONLY = 1U << 2,
    /** Wrap visual rows at the viewport width without changing text bytes. */
    REIST_GUI_TEXT_EDITOR_VIRTUAL_WRAP = 1U << 3
};

enum reist_gui_text_editor_event_type {
    REIST_GUI_TEXT_EDITOR_EVENT_POINTER_MOTION = 1U,
    REIST_GUI_TEXT_EDITOR_EVENT_POINTER_BUTTON,
    REIST_GUI_TEXT_EDITOR_EVENT_KEYBOARD,
    REIST_GUI_TEXT_EDITOR_EVENT_TEXT,
    REIST_GUI_TEXT_EDITOR_EVENT_FOCUS,
    REIST_GUI_TEXT_EDITOR_EVENT_CANCEL
};

enum reist_gui_text_editor_key {
    REIST_GUI_TEXT_EDITOR_KEY_LEFT = 1U,
    REIST_GUI_TEXT_EDITOR_KEY_RIGHT,
    REIST_GUI_TEXT_EDITOR_KEY_UP,
    REIST_GUI_TEXT_EDITOR_KEY_DOWN,
    REIST_GUI_TEXT_EDITOR_KEY_HOME,
    REIST_GUI_TEXT_EDITOR_KEY_END,
    REIST_GUI_TEXT_EDITOR_KEY_PAGE_UP,
    REIST_GUI_TEXT_EDITOR_KEY_PAGE_DOWN,
    REIST_GUI_TEXT_EDITOR_KEY_BACKSPACE,
    REIST_GUI_TEXT_EDITOR_KEY_DELETE,
    REIST_GUI_TEXT_EDITOR_KEY_ENTER,
    REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_HOME,
    REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_END
};

#define REIST_GUI_TEXT_EDITOR_BUTTON_LEFT 1U

/** Immutable identity, geometry and font-cell contract. */
typedef struct reist_gui_text_editor_model {
    uint32_t version;     /**< REIST_GUI_TEXT_EDITOR_API_VERSION. */
    uint32_t struct_size; /**< sizeof(reist_gui_text_editor_model_t). */
    uint32_t id;          /**< Stable nonzero semantic identity. */
    const char *name;     /**< Caller-owned accessible name. */
    reist_gui_rect_t bounds; /**< Local text viewport. */
    uint32_t glyph_width; /**< Nonzero fixed font-cell width in pixels. */
    uint32_t glyph_height; /**< Nonzero fixed font-cell height in pixels. */
    uint32_t flags;       /**< REIST_GUI_TEXT_EDITOR_* mask. */
    uint32_t reserved[4]; /**< Must be zero. */
} reist_gui_text_editor_model_t;

/** Caller-owned document, scalar cursor/viewport and pointer-grab state. */
typedef struct reist_gui_text_editor_state {
    uint32_t version;
    uint32_t struct_size;
    uint32_t configured;
    uint32_t focused;
    uint32_t captured;
    uint32_t modified;
    uint32_t line_count;
    uint32_t cursor_line;
    uint32_t cursor_column;
    uint32_t preferred_column;
    uint32_t first_line;
    uint32_t first_column;
    char lines[REIST_GUI_TEXT_EDITOR_MAX_LINES]
              [REIST_GUI_TEXT_EDITOR_LINE_CAPACITY];
    uint32_t reserved[4];
} reist_gui_text_editor_state_t;

/** One normalized event. TEXT accepts one printable Unicode scalar. */
typedef struct reist_gui_text_editor_event {
    uint32_t version;
    uint32_t struct_size;
    uint32_t type;
    int32_t x;
    int32_t y;
    uint32_t button;
    uint32_t pressed;
    uint32_t key;
    uint32_t codepoint;
    uint32_t focused;
    uint32_t reserved[4];
} reist_gui_text_editor_event_t;

/** Semantic output plus bounded repaint requests in model coordinates. */
typedef struct reist_gui_text_editor_result {
    uint32_t version;
    uint32_t struct_size;
    uint32_t consumed;
    uint32_t changed;
    uint32_t focus_changed;
    uint32_t control_id;
    uint32_t modified;
    uint32_t cursor_line;
    uint32_t cursor_column;
    uint32_t first_line;
    uint32_t first_column;
    reist_gui_rect_t damage[REIST_GUI_TEXT_EDITOR_DAMAGE_CAPACITY];
    uint32_t damage_count;
    uint32_t full_redraw;
    uint32_t reserved[4];
} reist_gui_text_editor_result_t;

/** Bounded document/viewport metrics for composing external scrollbars. */
typedef struct reist_gui_text_editor_viewport {
    uint32_t version;
    uint32_t struct_size;
    uint32_t visible_lines;
    uint32_t visible_columns;
    uint32_t maximum_first_line;
    uint32_t maximum_first_column;
    uint32_t first_line;
    uint32_t first_column;
    uint32_t reserved[4];
} reist_gui_text_editor_viewport_t;

/** Initialize an unconfigured empty state. @param[out] state NULL-safe. */
void reist_gui_text_editor_state_initialize(
    reist_gui_text_editor_state_t *state);
/** Initialize a normalized event. @param[out] event NULL-safe. */
void reist_gui_text_editor_event_initialize(
    reist_gui_text_editor_event_t *event);
/** Initialize a result before each call. @param[out] result NULL-safe. */
void reist_gui_text_editor_result_initialize(
    reist_gui_text_editor_result_t *result);

/** Validate the complete bounded model/state pair.
 * @return OK or EINVAL without mutation. */
int reist_gui_text_editor_validate(
    const reist_gui_text_editor_model_t *model,
    const reist_gui_text_editor_state_t *state);

/** Configure one empty document and request a complete repaint.
 * @return OK or an error before state mutation. */
int reist_gui_text_editor_configure(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    reist_gui_text_editor_result_t *result);

/** Replace the document after validating the complete input first.
 * CRLF, CR and LF are accepted and normalized to lines; other bytes must form
 * printable RFC 3629 UTF-8 scalars. The cursor and modified flag are reset.
 * @param[in] text Caller-owned bytes, valid for this call.
 * @param[in] length Number of input bytes.
 * @return OK, EINVAL or ECAPACITY without partial replacement. */
int reist_gui_text_editor_set_text(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    const char *text, size_t length,
    reist_gui_text_editor_result_t *result);

/** Serialize with LF separators and a trailing NUL not counted in length.
 * @param[out] text Destination with at least required bytes plus NUL.
 * @param[in] capacity Destination byte capacity.
 * @param[out] length_out Serialized length on success.
 * @return OK or an error without partial output. */
int reist_gui_text_editor_get_text(
    const reist_gui_text_editor_model_t *model,
    const reist_gui_text_editor_state_t *state,
    char *text, size_t capacity, size_t *length_out);

/** Clear the modified flag after the caller durably saved the document.
 * The library performs no persistence itself. A complete editor repaint is
 * requested so title and status renderers can remove their dirty marker.
 * @return OK or EINVAL before mutation. */
int reist_gui_text_editor_mark_saved(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    reist_gui_text_editor_result_t *result);

/** Query clamped viewport limits for scrollbar composition.
 * The maximum column is derived from the longest validated document line.
 * Work is bounded by the fixed line and line-byte capacities.
 * @return OK or EINVAL without mutation. */
int reist_gui_text_editor_get_viewport(
    const reist_gui_text_editor_model_t *model,
    const reist_gui_text_editor_state_t *state,
    reist_gui_text_editor_viewport_t *viewport);

/** Map one absolute visual row to a logical line and scalar column.
 * With virtual wrap disabled, each logical line is one visual row and the
 * returned column is zero. Work is bounded by fixed document capacities.
 * @return OK or EINVAL without mutation. */
int reist_gui_text_editor_visual_row(
    const reist_gui_text_editor_model_t *model,
    const reist_gui_text_editor_state_t *state,
    uint32_t visual_row, uint32_t *line_out,
    uint32_t *first_column_out);

/** Set the viewport origin without moving the cursor or editing the document.
 * Requested origins are clamped to the current document and visible-cell
 * limits. A changed origin requests one complete editor repaint.
 * @return OK or EINVAL before mutation. */
int reist_gui_text_editor_scroll_to(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    uint32_t first_line, uint32_t first_column,
    reist_gui_text_editor_result_t *result);

/** Dispatch one event through focus, pointer capture and editing semantics.
 * @return OK or EINVAL before mutation for malformed input. */
int reist_gui_text_editor_dispatch(
    const reist_gui_text_editor_model_t *model,
    reist_gui_text_editor_state_t *state,
    const reist_gui_text_editor_event_t *event,
    reist_gui_text_editor_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* REIST_GUI_TEXT_EDITOR_H */
