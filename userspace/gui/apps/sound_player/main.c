/**
 * @file userspace/gui/apps/sound_player/main.c
 * @brief Bounded graphical WAV player using delegated Surface and audio IPC.
 *
 * The player owns no global display or input authority. The compositor
 * delegates one fixed-capacity Surface endpoint and keeps running its own
 * lifecycle heartbeat while this independent client plays audio.
 */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "reist/audio.h"
#include "reist/audio_wave.h"
#include "reist/gui/control.h"
#include "reist/gui/surface_client.h"

#define PLAYER_PREVIEW_FRAMES 15360U
#define PLAYER_SURFACE_EVENT_BATCH_LIMIT 32U
#define PLAYER_SURFACE_CREATE_ATTEMPTS 250U
#define PLAYER_AUDIO_TRANSACTION_MS REIST_AUDIO_DEFAULT_TIMEOUT_MS
#define PLAYER_DRAIN_GUARD_MS 20U
#define PLAYER_DEFAULT_WIDTH 640U
#define PLAYER_DEFAULT_HEIGHT 320U
#define PLAYER_MIN_WIDTH 400U
#define PLAYER_MIN_HEIGHT 240U
#define PLAYER_FONT_WIDTH 8U
#define PLAYER_FONT_HEIGHT 16U
#define PLAYER_TEXT_LIMIT 256U
#define PLAYER_KEY_ESCAPE 0x101U

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
    uint64_t playback_started_ms;
    uint64_t playback_deadline_ms;
    uint32_t exit_requested;
    uint32_t redraw;
} player_state_t;

/* The service contract permits 15360 frames; static storage keeps memory
 * bounded and outside the small Ring-3 stack. */
static int16_t samples[PLAYER_PREVIEW_FRAMES * REIST_AUDIO_CHANNELS];

static const uint32_t color_face = 0x00C8C8C8U;
static const uint32_t color_light = 0x00FFFFFFU;
static const uint32_t color_shadow = 0x00606060U;
static const uint32_t color_dark = 0x00181818U;
static const uint32_t color_active = 0x00000088U;
static const uint32_t color_text = 0x00000000U;
static const uint32_t color_title = 0x00FFFFFFU;

static void report_audio_failure(const char *stage, int status) {
    x86os_puts("SOUNDPLAYER_AUDIO_FAIL stage=");
    x86os_puts(stage);
    x86os_puts(" status=");
    x86os_print_number(status);
    x86os_putchar('\n');
}

static size_t bounded_length(const char *value, size_t capacity) {
    size_t length = 0U;
    if (value == NULL) return 0U;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static void bytes_zero(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t index = 0U; index < length; ++index) bytes[index] = 0U;
}

static uint32_t starts_with(const char *value, const char *prefix) {
    if (value == NULL || prefix == NULL) return 0U;
    while (*prefix != '\0')
        if (*value++ != *prefix++) return 0U;
    return 1U;
}

static uint32_t text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == NULL || right == NULL) return 0U;
    while (index < PLAYER_TEXT_LIMIT && left[index] != '\0' &&
           right[index] != '\0') {
        if (left[index] != right[index]) return 0U;
        ++index;
    }
    return index < PLAYER_TEXT_LIMIT && left[index] == right[index];
}

static const char *wave_path_from_argv(int argc, char **argv) {
    const char *path = NULL;
    if (argv == NULL) return NULL;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == NULL ||
            starts_with(argv[index], "--reist-surface=")) continue;
        if (path != NULL) return NULL;
        path = argv[index];
    }
    return path;
}

static uint32_t max_u32(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static int paint_fill(reist_gui_surface_client_t *client,
                      reist_gui_rect_t rect, uint32_t color) {
    if (rect.width == 0U || rect.height == 0U) return 0;
    return reist_gui_surface_client_paint_fill(client, rect, color);
}

static int paint_text(reist_gui_surface_client_t *client,
                      int32_t x, int32_t y, const char *value,
                      uint32_t width, uint32_t foreground,
                      uint32_t background) {
    if (client == NULL || value == NULL || width == 0U) return 0;
    size_t length = bounded_length(value, PLAYER_TEXT_LIMIT);
    size_t capacity = width / PLAYER_FONT_WIDTH;
    if (length > capacity) length = capacity;
    if (length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY)
        length = REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U;
    if (length == 0U) return 0;
    return reist_gui_surface_client_paint_text(
        client, x, y, width, value, (uint32_t)length,
        foreground, background);
}

static int bevel(reist_gui_surface_client_t *client,
                 reist_gui_rect_t rect, uint32_t raised) {
    if (paint_fill(client, rect, color_face) != 0) return -1;
    if (rect.width < 2U || rect.height < 2U) return 0;
    uint32_t first = raised ? color_light : color_shadow;
    uint32_t second = raised ? color_shadow : color_light;
    if (paint_fill(client,
            (reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, first) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, first) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                               rect.width, 1U}, second) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y,
                               1U, rect.height}, second) != 0) return -1;
    return 0;
}

static int outline(reist_gui_surface_client_t *client,
                   reist_gui_rect_t rect, uint32_t color) {
    if (paint_fill(client,
            (reist_gui_rect_t){rect.x, rect.y, rect.width, 1U}, color) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){rect.x, rect.y, 1U, rect.height}, color) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){rect.x, rect.y + (int32_t)rect.height - 1,
                               rect.width, 1U}, color) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){rect.x + (int32_t)rect.width - 1, rect.y,
                               1U, rect.height}, color) != 0) return -1;
    return 0;
}

static reist_gui_rect_t player_frame(uint32_t width, uint32_t height) {
    return (reist_gui_rect_t){6, 6, width - 12U, height - 12U};
}

static int configure_controls(player_state_t *state,
                              uint32_t width, uint32_t height) {
    if (state == NULL || width < PLAYER_MIN_WIDTH ||
        height < PLAYER_MIN_HEIGHT) return -22;
    reist_gui_rect_t frame = player_frame(width, height);
    uint32_t button_height = max_u32(PLAYER_FONT_HEIGHT + 12U, 28U);
    uint32_t button_width = 112U;
    int32_t button_y = frame.y + (int32_t)frame.height -
        (int32_t)button_height - 18;
    int32_t center = frame.x + (int32_t)(frame.width / 2U);
    uint32_t common = REIST_GUI_CONTROL_VISIBLE | REIST_GUI_CONTROL_ENABLED;
    state->controls[0] = (reist_gui_control_t){PLAYER_CONTROL_PLAY,
        REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Abspielen",
        {center - 176, button_y, button_width, button_height},
        PLAYER_ACTION_PLAY, 0U, common | REIST_GUI_CONTROL_DEFAULT, 0U,
        {0U, 0U}};
    state->controls[1] = (reist_gui_control_t){PLAYER_CONTROL_STOP,
        REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Stop",
        {center - 56, button_y, button_width, button_height},
        PLAYER_ACTION_STOP, 0U, common, 0U, {0U, 0U}};
    state->controls[2] = (reist_gui_control_t){PLAYER_CONTROL_CLOSE,
        REIST_GUI_CONTROL_ROLE_PUSH_BUTTON, "Schliessen",
        {center + 64, button_y, button_width, button_height},
        PLAYER_ACTION_CLOSE, 0U, common, 0U, {0U, 0U}};
    state->model = (reist_gui_control_model_t){
        REIST_GUI_CONTROL_API_VERSION, sizeof(reist_gui_control_model_t),
        state->controls, 3U, width, height, 4U, {0U, 0U, 0U, 0U}};
    reist_gui_control_state_initialize(&state->control);
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    return reist_gui_control_configure(
        &state->model, &state->control, &result);
}

static int render(reist_gui_surface_client_t *client,
                  player_state_t *state) {
    if (client == NULL || state == NULL ||
        reist_gui_surface_client_paint_begin(client) != 0) return -1;
    reist_gui_rect_t frame = player_frame(client->width, client->height);
    if (paint_fill(client,
            (reist_gui_rect_t){frame.x + 4, frame.y + 4,
                               frame.width, frame.height}, color_dark) != 0 ||
        bevel(client, frame, 1U) != 0 ||
        paint_fill(client,
            (reist_gui_rect_t){frame.x + 4, frame.y + 4, frame.width - 8U,
                               max_u32(PLAYER_FONT_HEIGHT + 10U, 28U)},
            color_active) != 0 ||
        paint_text(client, frame.x + 14, frame.y + 10,
            "REIST Sound Player", frame.width - 28U,
            color_title, color_active) != 0 ||
        paint_text(client, frame.x + 24, frame.y + 68, state->path,
            frame.width - 48U, color_text, color_face) != 0 ||
        paint_text(client, frame.x + 24, frame.y + 104,
            state->wave.source_channels == 1U
                ? "WAV PCM 48 kHz / 16 Bit / Mono -> Stereo"
                : "WAV PCM 48 kHz / 16 Bit / Stereo",
            frame.width - 48U, color_text, color_face) != 0 ||
        paint_text(client, frame.x + 24, frame.y + 140, state->status,
            frame.width - 48U, color_text, color_face) != 0) return -1;

    for (uint32_t index = 0U; index < state->model.control_count; ++index) {
        reist_gui_control_t *control = &state->controls[index];
        uint32_t pressed = state->control.captured == index &&
            state->control.armed;
        if (state->control.focused == index &&
            outline(client,
                (reist_gui_rect_t){control->bounds.x - 2,
                    control->bounds.y - 2, control->bounds.width + 4U,
                    control->bounds.height + 4U}, color_dark) != 0) return -1;
        if (bevel(client, control->bounds, !pressed) != 0) return -1;
        uint32_t label_width = (uint32_t)bounded_length(
            control->label, REIST_GUI_CONTROL_LABEL_LIMIT) *
            PLAYER_FONT_WIDTH;
        if (paint_text(client,
                control->bounds.x + (int32_t)((control->bounds.width >
                    label_width ? control->bounds.width - label_width : 0U) /
                    2U),
                control->bounds.y + (int32_t)((control->bounds.height >
                    PLAYER_FONT_HEIGHT ? control->bounds.height -
                    PLAYER_FONT_HEIGHT : 0U) / 2U),
                control->label, control->bounds.width,
                color_text, color_face) != 0) return -1;
    }
    return reist_gui_surface_client_paint_commit(client);
}

static int stop_audio(player_state_t *state) {
    int result = 0;
    if (state->playing)
        result = reist_audio_stop(&state->audio, state->stream);
    state->playing = 0U;
    state->playback_started_ms = 0U;
    state->playback_deadline_ms = 0U;
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
        if (result != 0) {
            report_audio_failure("connect", result);
            return result;
        }
        state->audio_initialized = 1U;
        result = reist_audio_set_timeout(
            &state->audio, PLAYER_AUDIO_TRANSACTION_MS);
        if (result != 0) {
            report_audio_failure("timeout", result);
            shutdown_audio(state);
            return result;
        }
    }
    const reist_audio_format_t format = {
        REIST_AUDIO_SAMPLE_RATE, REIST_AUDIO_CHANNELS,
        REIST_AUDIO_FORMAT_S16_LE};
    result = reist_audio_open(&state->audio, &format, &state->stream);
    if (result != 0) {
        report_audio_failure("open", result);
        shutdown_audio(state);
        return result;
    }
    int written = reist_audio_write(
        &state->audio, state->stream, samples, state->wave.loaded_frames);
    if (written != (int)state->wave.loaded_frames) {
        report_audio_failure("write", written);
        shutdown_audio(state);
        return written < 0 ? written : -5;
    }
    const uint32_t frames_per_ms = REIST_AUDIO_SAMPLE_RATE / 1000U;
    uint64_t duration_ms =
        (state->wave.loaded_frames + frames_per_ms - 1U) / frames_per_ms +
        PLAYER_DRAIN_GUARD_MS;
    if (frames_per_ms == 0U) {
        report_audio_failure("clock", -75);
        shutdown_audio(state);
        return -75;
    }
    int started = reist_audio_start(&state->audio, state->stream);
    if (started == 0) {
        uint64_t now_ms = 0U;
        if (x86os_monotonic_ms(&now_ms) != 0 ||
            now_ms > UINT64_MAX - duration_ms) {
            report_audio_failure("clock", -75);
            shutdown_audio(state);
            return -75;
        }
        state->playing = 1U;
        state->playback_started_ms = now_ms;
        state->playback_deadline_ms = now_ms + duration_ms;
        state->status = "Status: Wiedergabe";
        x86os_puts("SOUNDPLAYER_PLAYBACK_OK\n");
    } else {
        report_audio_failure("start", started);
        shutdown_audio(state);
        state->status = "Status: Audiofehler";
    }
    return started;
}

static void poll_playback(player_state_t *state) {
    if (state == NULL || !state->playing) return;
    uint64_t now_ms = 0U;
    int clock_status = x86os_monotonic_ms(&now_ms);
    uint32_t completed = clock_status == 0 &&
        now_ms >= state->playback_started_ms &&
        now_ms >= state->playback_deadline_ms;
    if (!completed && clock_status == 0 &&
        now_ms >= state->playback_started_ms) return;
    if (!completed) report_audio_failure("clock", -84);
    int result = stop_audio(state);
    if (completed && result == 0) {
        state->status = "Status: Beendet";
        x86os_puts("SOUNDPLAYER_PLAYBACK_DONE\n");
    } else {
        if (result != 0) report_audio_failure("auto-stop", result);
        state->status = "Status: Stopfehler";
    }
    state->redraw = 1U;
}

static void handle_action(player_state_t *state, uint32_t action) {
    if (action == PLAYER_ACTION_PLAY) {
        int result = begin_audio(state);
        state->status = result == 0 ? "Status: Wiedergabe"
                                    : "Status: Audiofehler";
    } else if (action == PLAYER_ACTION_STOP) {
        int result = stop_audio(state);
        state->status = result == 0 ? "Status: Gestoppt"
                                    : "Status: Stopfehler";
    } else if (action == PLAYER_ACTION_CLOSE) {
        state->exit_requested = 1U;
    }
    state->redraw = 1U;
}

static void dispatch(player_state_t *state,
                     reist_gui_control_event_t *event) {
    reist_gui_control_result_t result;
    reist_gui_control_result_initialize(&result);
    if (reist_gui_control_dispatch(
            &state->model, &state->control, event, &result) == 0) {
        if (result.activated) handle_action(state, result.action);
        if (result.damage_count != 0U || result.full_redraw)
            state->redraw = 1U;
    }
}

static void handle_surface_input(player_state_t *state,
                                 const reist_gui_surface_input_t *input) {
    if (state == NULL || input == NULL) return;
    reist_gui_control_event_t event;
    reist_gui_control_event_initialize(&event);
    if (input->type == REIST_GUI_SURFACE_INPUT_POINTER_MOTION) {
        event.type = REIST_GUI_CONTROL_EVENT_POINTER_MOTION;
        event.x = input->x;
        event.y = input->y;
    } else if (input->type == REIST_GUI_SURFACE_INPUT_POINTER_BUTTON) {
        event.type = REIST_GUI_CONTROL_EVENT_POINTER_BUTTON;
        event.x = input->x;
        event.y = input->y;
        event.button = REIST_GUI_CONTROL_BUTTON_LEFT;
        event.pressed = input->pressed;
    } else if (input->type == REIST_GUI_SURFACE_INPUT_KEYBOARD &&
               input->pressed) {
        if (input->key == PLAYER_KEY_ESCAPE) {
            state->exit_requested = 1U;
            return;
        }
        event.type = REIST_GUI_CONTROL_EVENT_KEYBOARD;
        if (input->key == '\t') event.key = REIST_GUI_CONTROL_KEY_NEXT;
        else if (input->key == ' ') event.key = REIST_GUI_CONTROL_KEY_SPACE;
        else if (input->key == '\r' || input->key == '\n')
            event.key = REIST_GUI_CONTROL_KEY_ENTER;
        else return;
    } else return;
    dispatch(state, &event);
}

int main(int argc, char **argv) {
    if (argc == 2 && argv != NULL && text_equal(argv[1], "--help")) {
        x86os_puts("Usage: soundplayer --reist-surface=<handle> "
                   "<pcm-wave-file>\n");
        return 0;
    }
    x86os_ipc_handle_t endpoint = 0U;
    const char *path = wave_path_from_argv(argc, argv);
    if (argc != 3 || path == NULL ||
        reist_gui_surface_endpoint_from_argv(argc, argv, &endpoint) != 0) {
        x86os_puts("soundplayer: compositor endpoint and WAV file required\n");
        return 2;
    }

    static player_state_t state;
    state.path = path;
    state.status = "Status: Bereit";
    int result = reist_audio_wave_load_preview(
        state.path, samples, PLAYER_PREVIEW_FRAMES, &state.wave);
    if (result != 0) {
        x86os_puts("soundplayer: WAV-Datei ungueltig\n");
        (void)x86os_ipc_release(endpoint);
        return 1;
    }

    /* Begin playback before any Surface construction or paint transaction.
     * The compositor remains independent while at most 31 bulk writes are
     * synchronously confirmed by this client. */
    int audio_result = begin_audio(&state);
    state.status = audio_result == 0 ? "Status: Wiedergabe"
                                     : "Status: Audiofehler";

    reist_gui_surface_client_t client;
    bytes_zero(&client, sizeof(client));
    result = reist_gui_surface_client_init(&client, endpoint);
    if (result == 0) {
        result = -9;
        for (uint32_t attempt = 0U;
             attempt < PLAYER_SURFACE_CREATE_ATTEMPTS; ++attempt) {
            result = reist_gui_surface_client_create(
                &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL,
                PLAYER_DEFAULT_WIDTH, PLAYER_DEFAULT_HEIGHT);
            if (result == 0 || (result != -9 && result != -13)) break;
            poll_playback(&state);
            (void)x86os_sleep_ms(1U);
        }
    }
    if (result == 0)
        result = reist_gui_surface_client_ack_configure(
            &client, client.configured_serial);
    if (result == 0)
        result = reist_gui_surface_client_set_title(
            &client, "REIST Sound Player");
    if (result == 0)
        result = configure_controls(&state, client.width, client.height);
    if (result == 0) result = render(&client, &state);
    if (result != 0) {
        if (client.connected)
            (void)reist_gui_surface_client_destroy(&client);
        shutdown_audio(&state);
        (void)x86os_ipc_release(endpoint);
        return 1;
    }

    x86os_puts("SOUNDPLAYER_SURFACE_READY\n");
    while (!state.exit_requested) {
        uint32_t processed = 0U;
        for (; processed < PLAYER_SURFACE_EVENT_BATCH_LIMIT; ++processed) {
            reist_gui_surface_message_t message;
            int receive = reist_gui_surface_client_receive(
                &client, &message, 0U);
            if (receive == -11) break;
            if (receive != 0) {
                result = receive;
                state.exit_requested = 1U;
                break;
            }
            if (message.type == REIST_GUI_SURFACE_CLOSE) {
                state.exit_requested = 1U;
            } else if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
                result = reist_gui_surface_client_accept_configure(
                    &client, &message);
                if (result == 0)
                    result = configure_controls(
                        &state, client.width, client.height);
                if (result != 0) state.exit_requested = 1U;
                else state.redraw = 1U;
            } else if (message.type == REIST_GUI_SURFACE_INPUT) {
                handle_surface_input(&state, &message.input);
            }
        }
        poll_playback(&state);
        if (state.redraw && !state.exit_requested) {
            result = render(&client, &state);
            state.redraw = 0U;
            if (result != 0) state.exit_requested = 1U;
        }
        if (!state.exit_requested && processed == 0U)
            (void)x86os_sleep_ms(5U);
    }

    shutdown_audio(&state);
    (void)reist_gui_surface_client_destroy(&client);
    (void)x86os_ipc_release(endpoint);
    return result == 0 ? 0 : 1;
}
