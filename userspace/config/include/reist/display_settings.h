#ifndef REIST_DISPLAY_SETTINGS_H
#define REIST_DISPLAY_SETTINGS_H
#include <stdint.h>
#include "reist/display_mode.h"
#ifdef __cplusplus
extern "C" {
#endif
/* NULL/missing means auto. Any error clears both outputs. No I/O/allocation. */
int reist_display_setting_parse(const char *value, uint32_t *width, uint32_t *height);
int reist_display_setting_supported(uint32_t width, uint32_t height,
                                    const reist_display_mode_request_t *caps);
#ifdef __cplusplus
}
#endif
#endif
