#ifndef REIST_MOUSE_MODEL_H
#define REIST_MOUSE_MODEL_H
#include "x86os.h"
#include "reist/mouse_settings.h"
typedef struct mouse_model {
    reist_mouse_settings_t draft, saved, pending;
    uint32_t writable, child_generation, cancel_sent, fatal;
    int child;
    uint64_t started_ms;
    const char *status;
    char values[5][16], bytes[REIST_CONFIG_FILE_CAPACITY];
    reist_config_document_t document;
} mouse_model_t;
void mouse_model_initialize(mouse_model_t *model);
int mouse_model_read(mouse_model_t *model, reist_mouse_settings_t *settings);
int mouse_model_save(mouse_model_t *model);
int mouse_model_poll(mouse_model_t *model);
int mouse_model_cancel(mouse_model_t *model);
#endif
