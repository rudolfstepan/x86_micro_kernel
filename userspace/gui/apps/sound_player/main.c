/**
 * @file userspace/gui/apps/sound_player/main.c
 * @brief Bounded graphical WAV player using the public GUI and audio SDKs.
 *
 * The desktop supervises this client and restores its composition after exit.
 * Until Surface IPC is available the client paints a centered application
 * panel into the active display; it never reaches into compositor internals.
 */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "reist/audio.h"
#include "reist/audio_wave.h"
#include "reist/gui/control.h"

#define PLAYER_PREVIEW_FRAMES 15360U
#define PLAYER_MOUSE_BATCH_LIMIT 32U
#define PLAYER_TEXT_LIMIT 256U

enum { PLAYER_ACTION_PLAY = 1U, PLAYER_ACTION_STOP, PLAYER_ACTION_CLOSE };
enum { PLAYER_CONTROL_PLAY = 1U, PLAYER_CONTROL_STOP, PLAYER_CONTROL_CLOSE };

typedef struct player_state {
    reist_gui_control_t controls[3];
    reist_gui_control_model_t model;
    reist_gui_control_state_t control;
    reist_audio_context_t audio;
    reist_audio_stream_t stream;
    reist_audio_wave_info_t wave;
    const char *path;
    const char *status;
    uint32_t audio_initialized;
    uint32_t playing;
    uint32_t uploading;
    uint32_t uploaded_frames;
    uint32_t exit_requested;
    uint32_t redraw;
} player_state_t;

/* The service contract permits 15360 frames; static storage keeps stack use bounded. */
static int16_t samples[PLAYER_PREVIEW_FRAMES * REIST_AUDIO_CHANNELS];

static const uint32_t color_face = 0x00C8C8C8U;
static const uint32_t color_light = 0x00FFFFFFU;
static const uint32_t color_shadow = 0x00606060U;
static const uint32_t color_dark = 0x00181818U;
static const uint32_t color_active = 0x00000088U;
static const uint32_t color_text = 0x00000000U;
static const uint32_t color_title = 0x00FFFFFFU;

static size_t bounded_length(const char *value, size_t capacity) {
    size_t length = 0U;
    if (value == NULL) return 0U;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == NULL || right == NULL) return 0U;
    while (index < PLAYER_TEXT_LIMIT && left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < PLAYER_TEXT_LIMIT && left[index] == right[index];
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static void fill(reist_gui_rect_t rect, uint32_t color) {
    if (rect.width != 0U && rect.height != 0U)
        (void)x86os_fill_rect(rect.x, rect.y, rect.width, rect.height, color);
}

static void text(const x86os_display_info_t *display, int32_t x, int32_t y,
                 const char *value, uint32_t width, uint32_t foreground,
                 uint32_t background) {
    if (display == NULL || value == NULL || display->font_width == 0U) return;
    size_t length = bounded_length(value, PLAYER_TEXT_LIMIT);
    size_t capacity = width / display->font_width;
    if (length > capacity) length = capacity;
    if (length != 0U)
        (void)x86os_draw_text_pixels(x, y, value, length, foreground, background);
}

static void bevel(reist_gui_rect_t rect, uint32_t raised) {
    fill(rect, color_face);
    if (rect.width < 2U || rect.height < 2U) return;
    uint32_t first = raised ? color_light : color_shadow;
    uint32_t second = raised ? color_shadow : color_light;
    fill((reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, first);
    fill((reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, first);
    fill((reist_gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                            rect.width, 1U}, second);
    fill((reist_gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y,
                            1U, rect.height}, second);
}

static void outline(reist_gui_rect_t rect, uint32_t color) {
    fill((reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, color);
    fill((reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, color);
    fill((reist_gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                            rect.width, 1U}, color);
    fill((reist_gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y,
                            1U, rect.height}, color);
}

static reist_gui_rect_t player_frame(const x86os_display_info_t *display) {
    uint32_t width = display->width > 680U ? 640U : display->width - 24U;
    uint32_t height = display->height > 340U ? 300U : display->height - 24U;
    return (reist_gui_rect_t){(int32_t)((display->width - width) / 2U),
        (int32_t)((display->height - height) / 2U), width, height};
}

static int configure_controls(player_state_t *state,
                              const x86os_display_info_t *display) {
    reist_gui_rect_t frame = player_frame(display);
    uint32_t button_height = max_u32(display->font_height + 12U, 28U);
    uint32_t button_width = 112U;
    int32_t button_y = frame.y + (int32_t)frame.height - (int32_t)button_height - 18;
    int32_t center = frame.x + (int32_t)(frame.width / 2U);
    uint32_t common = REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED;
    state->controls[0] = (reist_gui_control_t){PLAYER_CONTROL_PLAY,
        REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Abspielen",
        {center - 176, button_y, button_width, button_height},
        PLAYER_ACTION_PLAY, 0U, common | REIST_GUI_CONTROL_DEFAULT, 0U, {0U, 0U}};
    state->controls[1] = (reist_gui_control_t){PLAYER_CONTROL_STOP,
        REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Stop",
        {center - 56, button_y, button_width, button_height},
        PLAYER_ACTION_STOP, 0U, common, 0U, {0U, 0U}};
    state->controls[2] = (reist_gui_control_t){PLAYER_CONTROL_CLOSE,
        REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Schliessen",
        {center + 64, button_y, button_width, button_height},
        PLAYER_ACTION_CLOSE, 0U, common, 0U, {0U, 0U}};
    state->model = (reist_gui_control_model_t){REIST_GUI_CONTROL_API_VERSION,
        sizeof(reist_gui_control_model_t), state->controls, 3U,
        display->width, display->height, 4U, {0U, 0U, 0U, 0U}};
    reist_gui_control_state_initialize(&state->control);
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    return reist_gui_control_configure(&state->model, &state->control, &result);
}

static void render(const x86os_display_info_t *display, player_state_t *state) {
    reist_gui_rect_t frame = player_frame(display);
    uint32_t serial = 0U;
    uint32_t transaction = x86os_display_frame_begin(&serial) == 0;
    fill((reist_gui_rect_t){frame.x + 6, frame.y + 6, frame.width, frame.height}, color_dark);
    bevel(frame, 1U);
    fill((reist_gui_rect_t){frame.x + 4, frame.y + 4, frame.width - 8U,
                            max_u32(display->font_height + 10U, 28U)}, color_active);
    text(display, frame.x + 14, frame.y + 10, "REIST Sound Player",
         frame.width - 28U, color_title, color_active);
    text(display, frame.x + 24, frame.y + 68, state->path,
         frame.width - 48U, color_text, color_face);
    text(display, frame.x + 24, frame.y + 104,
         state->wave.source_channels == 1U
             ? "WAV PCM 48 kHz / 16 Bit / Mono -> Stereo"
             : "WAV PCM 48 kHz / 16 Bit / Stereo",
         frame.width - 48U, color_text, color_face);
    text(display, frame.x + 24, frame.y + 140, state->status,
         frame.width - 48U, color_text, color_face);
    for (uint32_t index = 0U; index < state->model.control_count; ++index) {
        reist_gui_control_t *control = &state->controls[index];
        uint32_t pressed = state->control.captured == index && state->control.armed;
        if (state->control.focused == index)
            outline((reist_gui_rect_t){control->bounds.x - 2,
                control->bounds.y - 2, control->bounds.width + 4U,
                control->bounds.height + 4U}, color_dark);
        bevel(control->bounds, !pressed);
        uint32_t label_width = (uint32_t)bounded_length(
            control->label, REIST_GUI_CONTROL_LABEL_LIMIT) * display->font_width;
        text(display, control->bounds.x + (int32_t)((control->bounds.width > label_width
                ? control->bounds.width - label_width : 0U) / 2U),
            control->bounds.y + (int32_t)((control->bounds.height > display->font_height
                ? control->bounds.height - display->font_height : 0U) / 2U),
            control->label, control->bounds.width, color_text, color_face);
    }
    if (transaction && x86os_display_frame_commit(serial) != 0)
        (void)x86os_display_frame_cancel(serial);
}

static int stop_audio(player_state_t *state) {
    int result = 0;
    if (state->playing) result = reist_audio_stop(&state->audio, state->stream);
    state->playing = 0U;
    state->uploading = 0U;
    state->uploaded_frames = 0U;
    if (state->stream.id != 0U) {
        int closed = reist_audio_close(&state->audio, &state->stream);
        if (result == 0) result = closed;
    }
    state->stream = (reist_audio_stream_t){0};
    return result;
}

static void shutdown_audio(player_state_t *state) {
    (void)stop_audio(state);
    if (state->audio_initialized) reist_audio_shutdown(&state->audio);
    state->audio_initialized = 0U;
}

static int begin_audio(player_state_t *state) {
    (void)stop_audio(state);
    int result = 0;
    if (!state->audio_initialized) {
        result = reist_audio_init(&state->audio);
        if (result != 0) return result;
        state->audio_initialized = 1U;
    }
    const reist_audio_format_t format = {REIST_AUDIO_SAMPLE_RATE,
        REIST_AUDIO_CHANNELS, REIST_AUDIO_FORMAT_S16_LE};
    result = reist_audio_open(&state->audio, &format, &state->stream);
    if (result == 0) {
        state->uploaded_frames = 0U;
        state->uploading = 1U;
    } else shutdown_audio(state);
    return result;
}

/* Transfer at most one protocol payload per GUI iteration.  This preserves
 * bounded input latency while a complete stream is staged in the service. */
static void pump_audio(player_state_t *state) {
    if (!state->uploading || state->stream.id == 0U) return;
    uint32_t remaining = state->wave.loaded_frames - state->uploaded_frames;
    uint32_t chunk = remaining < REIST_AUDIO_MESSAGE_FRAMES
        ? remaining : REIST_AUDIO_MESSAGE_FRAMES;
    int written = reist_audio_write(
        &state->audio, state->stream,
        &samples[state->uploaded_frames * REIST_AUDIO_CHANNELS], chunk);
    if (written != (int)chunk) {
        shutdown_audio(state);
        state->status = "Status: Audiofehler";
        state->redraw = 1U;
        return;
    }
    state->uploaded_frames += chunk;
    if (state->uploaded_frames != state->wave.loaded_frames) return;
    state->uploading = 0U;
    if (reist_audio_start(&state->audio, state->stream) == 0) {
        state->playing = 1U;
        state->status = "Status: Wiedergabe";
    } else {
        shutdown_audio(state);
        state->status = "Status: Audiofehler";
    }
    state->redraw = 1U;
}

static void handle_action(player_state_t *state, uint32_t action) {
    if (action == PLAYER_ACTION_PLAY) {
        int result = begin_audio(state);
        state->status = result == 0 ? "Status: Laden..." : "Status: Audiofehler";
    } else if (action == PLAYER_ACTION_STOP) {
        int result = stop_audio(state);
        state->status = result == 0 ? "Status: Gestoppt" : "Status: Stopfehler";
    } else if (action == PLAYER_ACTION_CLOSE) {
        state->exit_requested = 1U;
    }
    state->redraw = 1U;
}

static void dispatch(player_state_t *state, reist_gui_control_event_t *event) {
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    if (reist_gui_control_dispatch(&state->model, &state->control, event, &result) == 0) {
        if (result.activated) handle_action(state, result.action);
        if (result.damage_count != 0U || result.full_redraw) state->redraw = 1U;
    }
}

static void move_pointer(const x86os_display_info_t *display, int32_t *x,
                         int32_t *y, int32_t dx, int32_t dy) {
    int64_t next_x = (int64_t)*x + dx, next_y = (int64_t)*y + dy;
    if (next_x < 0) next_x = 0;
    if (next_y < 0) next_y = 0;
    if (next_x >= display->width) next_x = display->width - 1U;
    if (next_y >= display->height) next_y = display->height - 1U;
    *x = (int32_t)next_x; *y = (int32_t)next_y;
}

int main(int argc, char **argv) {
    if (argc == 2 && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: soundplayer <pcm-wave-file>\n");
        return 0;
    }
    if (argc != 2) { x86os_puts("Usage: soundplayer <pcm-wave-file>\n"); return 2; }
    static player_state_t state;
    state.path = argv[1]; state.status = "Status: Bereit";
    int result = reist_audio_wave_load_preview(
        state.path, samples, PLAYER_PREVIEW_FRAMES, &state.wave);
    if (result != 0) { x86os_puts("soundplayer: WAV-Datei ungueltig\n"); return 1; }
    x86os_display_info_t display;
    uint32_t runtime_activated = 0U;
    if (x86os_display_info(&display) != 0) {
        if (x86os_display_activate() == 0) runtime_activated = 1U;
        if (x86os_display_info(&display) != 0) return 1;
    }
    if (display.version != X86OS_DISPLAY_ABI_VERSION ||
        display.struct_size < sizeof(display) || display.width < 400U ||
        display.height < 240U || configure_controls(&state, &display) != 0) {
        if (runtime_activated) (void)x86os_display_deactivate();
        return 1;
    }
    int32_t pointer_x = (int32_t)(display.width / 2U);
    int32_t pointer_y = (int32_t)(display.height / 2U);
    uint32_t previous_buttons = 0U;
    render(&display, &state);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
    while (!state.exit_requested) {
        uint32_t mouse_count = 0U;
        for (; mouse_count < PLAYER_MOUSE_BATCH_LIMIT; ++mouse_count) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            move_pointer(&display, &pointer_x, &pointer_y, mouse.delta_x, mouse.delta_y);
            reist_gui_control_event_t event;
            reist_gui_control_event_initialize(&event);
            event.type = REIST_GUI_CONTROL_EVENT_POINTER_MOTION;
            event.x = pointer_x; event.y = pointer_y;
            dispatch(&state, &event);
            uint32_t left = (mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            uint32_t previous = (previous_buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U;
            if (left != previous) {
                reist_gui_control_event_initialize(&event);
                event.type = REIST_GUI_CONTROL_EVENT_POINTER_BUTTON;
                event.x = pointer_x; event.y = pointer_y;
                event.button = REIST_GUI_CONTROL_BUTTON_LEFT; event.pressed = left;
                dispatch(&state, &event);
            }
            previous_buttons = mouse.buttons;
        }
        int key = x86os_getchar_nonblocking();
        if (key == 0x1B) state.exit_requested = 1U;
        else if (key == '\t' || key == '\r' || key == ' ') {
            reist_gui_control_event_t event;
            reist_gui_control_event_initialize(&event);
            event.type = REIST_GUI_CONTROL_EVENT_KEYBOARD;
            event.key = key == '\t' ? REIST_GUI_CONTROL_KEY_NEXT
                : key == ' ' ? REIST_GUI_CONTROL_KEY_SPACE : REIST_GUI_CONTROL_KEY_ENTER;
            dispatch(&state, &event);
        }
        pump_audio(&state);
        if (state.redraw) {
            (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
            render(&display, &state); state.redraw = 0U;
            (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        } else if (mouse_count != 0U) (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        else if (!state.uploading) (void)x86os_sleep_ms(5U);
    }
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    shutdown_audio(&state);
    if (runtime_activated) return x86os_display_deactivate() == 0 ? 0 : 1;
    x86os_clear();
    return 0;
}
