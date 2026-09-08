#ifndef REIST_MOUSE_SETTINGS_H
#define REIST_MOUSE_SETTINGS_H
#include <stdint.h>
#include "reist/config.h"
#ifdef __cplusplus
extern "C" {
#endif
#define REIST_MOUSE_SETTINGS_VERSION 1U
#define REIST_MOUSE_SETTING_COUNT 5U
enum reist_mouse_profile { REIST_MOUSE_FLAT, REIST_MOUSE_ADAPTIVE, REIST_MOUSE_OFF };
typedef struct reist_mouse_settings {
    uint32_t version, struct_size;
    uint32_t primary_right, speed_percent, acceleration, natural_scroll, double_click_ms;
} reist_mouse_settings_t;
typedef struct reist_mouse_motion {
    int32_t remainder_x, remainder_y;
    uint32_t generation, clock_valid;
    uint64_t previous_ms;
} reist_mouse_motion_t;
extern const char *const reist_mouse_keys[REIST_MOUSE_SETTING_COUNT];
void reist_mouse_settings_defaults(reist_mouse_settings_t *settings);
int reist_mouse_settings_valid(const reist_mouse_settings_t *settings);
/* Whole-candidate admission: invalid values leave safe defaults, never a mix. */
int reist_mouse_settings_parse(const reist_config_document_t *document, reist_mouse_settings_t *settings);
int reist_mouse_settings_format(const reist_mouse_settings_t *settings, char values[5][16]);
void reist_mouse_motion_apply(const reist_mouse_settings_t *settings, reist_mouse_motion_t *state,
    uint32_t generation, uint64_t now_ms, uint32_t clock_valid, int32_t *dx, int32_t *dy);
uint32_t reist_mouse_buttons(const reist_mouse_settings_t *settings, uint32_t buttons);
int32_t reist_mouse_wheel(const reist_mouse_settings_t *settings, int32_t wheel);
#ifdef __cplusplus
}
#endif
#endif
