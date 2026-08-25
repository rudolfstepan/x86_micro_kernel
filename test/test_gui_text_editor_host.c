#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "reist/gui/text_editor.h"

static const reist_gui_text_editor_model_t model = {
    REIST_GUI_TEXT_EDITOR_API_VERSION,
    sizeof(reist_gui_text_editor_model_t),
    10U,
    "Dokument",
    {8, 12, 320U, 160U},
    8U,
    16U,
    REIST_GUI_TEXT_EDITOR_VISIBLE | REIST_GUI_TEXT_EDITOR_ENABLED,
    {0U, 0U, 0U, 0U},
};

static void initialize(reist_gui_text_editor_state_t *state) {
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_state_initialize(state);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_configure(&model, state, &result) == 0);
    assert(result.full_redraw && state->line_count == 1U);
}

static void focus(reist_gui_text_editor_state_t *state) {
    reist_gui_text_editor_event_t event;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_event_initialize(&event);
    event.type = REIST_GUI_TEXT_EDITOR_EVENT_FOCUS;
    event.focused = 1U;
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_dispatch(
        &model, state, &event, &result) == 0);
    assert(state->focused && result.focus_changed);
}

static void key(reist_gui_text_editor_state_t *state, uint32_t key_value) {
    reist_gui_text_editor_event_t event;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_event_initialize(&event);
    event.type = REIST_GUI_TEXT_EDITOR_EVENT_KEYBOARD;
    event.key = key_value;
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_dispatch(
        &model, state, &event, &result) == 0);
    assert(result.consumed);
}

static void scalar(reist_gui_text_editor_state_t *state, uint32_t value) {
    reist_gui_text_editor_event_t event;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_event_initialize(&event);
    event.type = REIST_GUI_TEXT_EDITOR_EVENT_TEXT;
    event.codepoint = value;
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_dispatch(
        &model, state, &event, &result) == 0);
    assert(result.consumed && result.changed);
}

static void text(reist_gui_text_editor_state_t *state, char value) {
    scalar(state, (uint8_t)value);
}

static void assert_document(const reist_gui_text_editor_state_t *state,
                            const char *expected) {
    char serialized[REIST_GUI_TEXT_EDITOR_SERIALIZED_CAPACITY];
    size_t length = 0U;
    assert(reist_gui_text_editor_get_text(
        &model, state, serialized, sizeof(serialized), &length) == 0);
    assert(length == strlen(expected));
    assert(strcmp(serialized, expected) == 0);
}

static void test_replace_normalizes_and_fails_closed(void) {
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    initialize(&state);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, "one\r\ntwo\rthree", 14U, &result) == 0);
    assert(state.line_count == 3U && !state.modified);
    assert_document(&state, "one\ntwo\nthree");

    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, "bad\ttext", 8U, &result) ==
        REIST_GUI_TEXT_EDITOR_EINVAL);
    assert_document(&state, "one\ntwo\nthree");
}

static void test_multiline_editing_and_navigation(void) {
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    initialize(&state);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, "one\ntwo", 7U, &result) == 0);
    focus(&state);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_END);
    text(&state, 'X');
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_ENTER);
    text(&state, 'A');
    assert_document(&state, "oneX\nA\ntwo");
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_BACKSPACE);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_BACKSPACE);
    assert_document(&state, "oneX\ntwo");
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_END);
    assert(state.cursor_line == 1U && state.cursor_column == 3U);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_DOCUMENT_HOME);
    assert(state.cursor_line == 0U && state.cursor_column == 0U);
}

static void test_pointer_places_cursor_and_serialization_is_bounded(void) {
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_event_t event;
    initialize(&state);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, "alpha\nbeta", 10U, &result) == 0);
    reist_gui_text_editor_event_initialize(&event);
    event.type = REIST_GUI_TEXT_EDITOR_EVENT_POINTER_BUTTON;
    event.x = model.bounds.x + 16;
    event.y = model.bounds.y + 20;
    event.button = REIST_GUI_TEXT_EDITOR_BUTTON_LEFT;
    event.pressed = 1U;
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_dispatch(
        &model, &state, &event, &result) == 0);
    assert(state.cursor_line == 1U && state.cursor_column == 2U);

    char small[4] = {'x', 'x', 'x', '\0'};
    size_t length = 99U;
    assert(reist_gui_text_editor_get_text(
        &model, &state, small, sizeof(small), &length) ==
        REIST_GUI_TEXT_EDITOR_ECAPACITY);
    assert(small[0] == 'x' && length == 99U);
}

static void test_line_capacity_is_rejected_before_replacement(void) {
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    char oversized[REIST_GUI_TEXT_EDITOR_LINE_CAPACITY];
    initialize(&state);
    for (uint32_t index = 0U; index < sizeof(oversized); ++index)
        oversized[index] = 'a';
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, oversized, sizeof(oversized), &result) ==
        REIST_GUI_TEXT_EDITOR_ECAPACITY);
    assert_document(&state, "");
}

static void test_saved_marker_changes_only_after_explicit_commit(void) {
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    initialize(&state);
    focus(&state);
    text(&state, 'x');
    assert(state.modified == 1U);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_mark_saved(&model, &state, &result) == 0);
    assert(state.modified == 0U && result.changed && result.full_redraw);
    assert_document(&state, "x");
}

static void test_utf8_scalars_roundtrip_and_edit_atomically(void) {
    static const char original[] =
        "A\xC3\x84\xE2\x82\xAC\xE4\xB8\xAD\xF0\x9F\x98\x80";
    static const char without_emoji[] =
        "A\xC3\x84\xE2\x82\xAC\xE4\xB8\xAD";
    static const char edited[] =
        "A\xCE\xA9\xE2\x82\xAC\xE4\xB8\xAD";
    static const char malformed[] = {(char)0xE2, (char)0x82};
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    initialize(&state);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, original, sizeof(original) - 1U, &result) == 0);
    focus(&state);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_END);
    assert(state.cursor_column == 5U);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_BACKSPACE);
    assert(state.cursor_column == 4U);
    assert_document(&state, without_emoji);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_HOME);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_RIGHT);
    key(&state, REIST_GUI_TEXT_EDITOR_KEY_DELETE);
    scalar(&state, 0x03A9U);
    assert(state.cursor_column == 2U);
    assert_document(&state, edited);

    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, malformed, sizeof(malformed), &result) ==
        REIST_GUI_TEXT_EDITOR_EINVAL);
    assert_document(&state, edited);
}

static void test_viewport_scrolling_is_clamped_and_document_neutral(void) {
    static const char document[] =
        "012345678901234567890123456789012345678901234567\n"
        "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11";
    reist_gui_text_editor_state_t state;
    reist_gui_text_editor_result_t result;
    reist_gui_text_editor_viewport_t viewport;
    initialize(&state);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_set_text(
        &model, &state, document, sizeof(document) - 1U, &result) == 0);
    assert(reist_gui_text_editor_get_viewport(
        &model, &state, &viewport) == 0);
    assert(viewport.visible_lines == 10U);
    assert(viewport.visible_columns == 40U);
    assert(viewport.maximum_first_line == 2U);
    assert(viewport.maximum_first_column == 8U);

    uint32_t cursor_line = state.cursor_line;
    uint32_t cursor_column = state.cursor_column;
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_scroll_to(
        &model, &state, UINT32_MAX, UINT32_MAX, &result) == 0);
    assert(state.first_line == 2U && state.first_column == 8U);
    assert(state.cursor_line == cursor_line &&
           state.cursor_column == cursor_column);
    assert(!state.modified && !result.changed && result.full_redraw);
    assert_document(&state, document);

    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_scroll_to(
        &model, &state, 2U, 8U, &result) == 0);
    assert(!result.full_redraw);
    reist_gui_text_editor_result_initialize(&result);
    assert(reist_gui_text_editor_scroll_to(
        &model, &state, 0U, 0U, &result) == 0);
    assert(state.first_line == 0U && state.first_column == 0U);
}

int main(void) {
    test_replace_normalizes_and_fails_closed();
    test_multiline_editing_and_navigation();
    test_pointer_places_cursor_and_serialization_is_bounded();
    test_line_capacity_is_rejected_before_replacement();
    test_saved_marker_changes_only_after_explicit_commit();
    test_utf8_scalars_roundtrip_and_edit_atomically();
    test_viewport_scrolling_is_clamped_and_document_neutral();
    return 0;
}
