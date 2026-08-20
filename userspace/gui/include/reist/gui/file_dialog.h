/**
 * @file reist/gui/file_dialog.h
 * @brief Bounded asynchronous file-open and save-as path dialog controller.
 *
 * The controller is renderer-independent, heap-free and performs no VFS I/O.
 * Applications validate and execute the returned absolute path. Version 1 is
 * an absolute ASCII path chooser; directory enumeration belongs to a later
 * portal/service extension and is deliberately not simulated inside this
 * renderer-neutral control.
 *
 * Ownership and lifetime: all descriptors, state, events and results are
 * caller-owned. Strings referenced by a model must remain valid while the
 * dialog is open. Calls are synchronous state transitions, but completion is
 * asynchronous from the application's point of view: open the controller,
 * route input through dispatch, and act only on a completed result. The API
 * performs no allocation, blocking, filesystem access or nested event loop.
 * Coordinates are signed client-local pixels and dimensions are pixels.
 * Reserved fields must be zero so future versions fail closed.
 */
#ifndef REIST_GUI_FILE_DIALOG_H
#define REIST_GUI_FILE_DIALOG_H

#include <stdint.h>
#include "reist/gui/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_GUI_FILE_DIALOG_API_VERSION 1U
#define REIST_GUI_FILE_DIALOG_PATH_CAPACITY 256U

enum reist_gui_file_dialog_status {
    REIST_GUI_FILE_DIALOG_OK = 0,
    REIST_GUI_FILE_DIALOG_EINVAL = -1,
    REIST_GUI_FILE_DIALOG_ECAPACITY = -2
};

enum reist_gui_file_dialog_mode {
    REIST_GUI_FILE_DIALOG_OPEN = 1U,
    REIST_GUI_FILE_DIALOG_SAVE
};

enum reist_gui_file_dialog_response {
    REIST_GUI_FILE_DIALOG_RESPONSE_NONE = 0U,
    REIST_GUI_FILE_DIALOG_RESPONSE_ACCEPT,
    REIST_GUI_FILE_DIALOG_RESPONSE_CANCEL
};

enum reist_gui_file_dialog_event_type {
    REIST_GUI_FILE_DIALOG_EVENT_POINTER_MOTION = 1U,
    REIST_GUI_FILE_DIALOG_EVENT_POINTER_BUTTON,
    REIST_GUI_FILE_DIALOG_EVENT_KEYBOARD,
    REIST_GUI_FILE_DIALOG_EVENT_TEXT
};

enum reist_gui_file_dialog_key {
    REIST_GUI_FILE_DIALOG_KEY_TAB = 1U,
    REIST_GUI_FILE_DIALOG_KEY_BACKSPACE,
    REIST_GUI_FILE_DIALOG_KEY_DELETE,
    REIST_GUI_FILE_DIALOG_KEY_LEFT,
    REIST_GUI_FILE_DIALOG_KEY_RIGHT,
    REIST_GUI_FILE_DIALOG_KEY_HOME,
    REIST_GUI_FILE_DIALOG_KEY_END,
    REIST_GUI_FILE_DIALOG_KEY_ENTER,
    REIST_GUI_FILE_DIALOG_KEY_ESCAPE
};

enum reist_gui_file_dialog_focus {
    REIST_GUI_FILE_DIALOG_FOCUS_PATH = 1U,
    REIST_GUI_FILE_DIALOG_FOCUS_ACCEPT,
    REIST_GUI_FILE_DIALOG_FOCUS_CANCEL
};

typedef struct reist_gui_file_dialog_model {
    uint32_t version;
    uint32_t struct_size;
    uint32_t mode;
    const char *title;
    const char *accept_label;
    uint32_t reserved[4];
} reist_gui_file_dialog_model_t;

typedef struct reist_gui_file_dialog_layout {
    uint32_t version;
    uint32_t struct_size;
    reist_gui_rect_t frame;
    reist_gui_rect_t title;
    reist_gui_rect_t path;
    reist_gui_rect_t accept_button;
    reist_gui_rect_t cancel_button;
    uint32_t glyph_width;
    uint32_t reserved[4];
} reist_gui_file_dialog_layout_t;

typedef struct reist_gui_file_dialog_state {
    uint32_t version;
    uint32_t struct_size;
    uint32_t visible;
    uint32_t focus;
    uint32_t capture;
    uint32_t length;
    uint32_t cursor;
    char path[REIST_GUI_FILE_DIALOG_PATH_CAPACITY];
    uint32_t reserved[4];
} reist_gui_file_dialog_state_t;

typedef struct reist_gui_file_dialog_event {
    uint32_t version;
    uint32_t struct_size;
    uint32_t type;
    int32_t x;
    int32_t y;
    uint32_t button;
    uint32_t pressed;
    uint32_t key;
    uint32_t codepoint;
    uint32_t reserved[4];
} reist_gui_file_dialog_event_t;

typedef struct reist_gui_file_dialog_result {
    uint32_t version;
    uint32_t struct_size;
    uint32_t consumed;
    uint32_t completed;
    uint32_t response;
    uint32_t full_redraw;
    char path[REIST_GUI_FILE_DIALOG_PATH_CAPACITY];
    uint32_t reserved[4];
} reist_gui_file_dialog_result_t;

/** Reset caller-owned state to a valid, closed version-1 dialog. */
void reist_gui_file_dialog_state_initialize(
    reist_gui_file_dialog_state_t *state);
/** Reset one caller-owned input event and stamp its ABI header. */
void reist_gui_file_dialog_event_initialize(
    reist_gui_file_dialog_event_t *event);
/** Reset one caller-owned transition result and stamp its ABI header. */
void reist_gui_file_dialog_result_initialize(
    reist_gui_file_dialog_result_t *result);
/**
 * Validate a complete model/layout/state tuple without changing it.
 *
 * @return REIST_GUI_FILE_DIALOG_OK, or EINVAL for an incompatible ABI,
 * invalid geometry, malformed string, state invariant or nonzero reserved
 * field.
 */
int reist_gui_file_dialog_validate(
    const reist_gui_file_dialog_model_t *model,
    const reist_gui_file_dialog_layout_t *layout,
    const reist_gui_file_dialog_state_t *state);
/**
 * Open the dialog with a nonempty absolute ASCII path.
 *
 * @param model Immutable caller-owned labels and mode.
 * @param layout Immutable client-local geometry for this presentation.
 * @param state Caller-owned state replaced atomically on success.
 * @param initial_path NUL-terminated absolute path copied into state.
 * @param result Initialized caller-owned result receiving redraw metadata.
 * @return OK, EINVAL, or ECAPACITY. Failure leaves state unchanged.
 */
int reist_gui_file_dialog_open(
    const reist_gui_file_dialog_model_t *model,
    const reist_gui_file_dialog_layout_t *layout,
    reist_gui_file_dialog_state_t *state, const char *initial_path,
    reist_gui_file_dialog_result_t *result);
/**
 * Apply exactly one pointer, keyboard or text event.
 *
 * A completed ACCEPT result owns a stable copy of the selected path. CANCEL
 * completes with an empty result path. The caller must close, load or save
 * only after completion and must keep routing all other application input
 * away from an application-modal instance.
 *
 * @return OK, EINVAL, or ECAPACITY. Validation failure does not publish a
 * partially mutated state.
 */
int reist_gui_file_dialog_dispatch(
    const reist_gui_file_dialog_model_t *model,
    const reist_gui_file_dialog_layout_t *layout,
    reist_gui_file_dialog_state_t *state,
    const reist_gui_file_dialog_event_t *event,
    reist_gui_file_dialog_result_t *result);

#ifdef __cplusplus
}
#endif
#endif
