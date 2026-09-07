/* Fixed-capacity Ring-3 Display settings and owned CONFIG child lifecycle. */
#ifndef REIST_DISPLAY_APPLET_MODEL_H
#define REIST_DISPLAY_APPLET_MODEL_H
#include "x86os.h"
#include "reist/config.h"
#include "reist/display_settings.h"

#define DISPLAY_CHOICE_CAPACITY 17U
#define DISPLAY_SAVE_DEADLINE_MS 5000U
typedef struct display_choice {
    uint32_t width, height;
    char value[16];
} display_choice_t;
typedef struct display_model {
    reist_display_mode_request_t caps;
    display_choice_t choices[DISPLAY_CHOICE_CAPACITY];
    uint32_t count, selected, writable, saved_width, saved_height;
    int child;
    uint32_t child_generation, pending_choice, cancel_sent;
    uint64_t started_ms;
    const char *status;
    char config_bytes[REIST_CONFIG_FILE_CAPACITY];
    reist_config_document_t config;
} display_model_t;
void display_model_initialize(display_model_t *model);
int display_model_save(display_model_t *model);
/* Nonblocking process poll; returns one on a visible state transition. */
int display_model_poll(display_model_t *model);
/* One owned-child cancellation attempt; never waits for a running process. */
int display_model_cancel(display_model_t *model);
void display_mode_text(char output[16], uint32_t width, uint32_t height);
#endif
