#include "reist/display_settings.h"

int reist_display_setting_parse(const char *value, uint32_t *width, uint32_t *height) {
    if (width) *width = 0U;
    if (height) *height = 0U;
    if (!width || !height) return -22;
    if (!value || (value[0]=='a' && value[1]=='u' && value[2]=='t' &&
                   value[3]=='o' && value[4]=='\0')) return 0;
    uint32_t pair[2] = {0U, 0U};
    uint32_t at = 0U;
    for (uint32_t part = 0U; part < 2U; ++part) {
        if (value[at] < '1' || value[at] > '9') return -22;
        uint32_t digits = 0U;
        while (value[at] >= '0' && value[at] <= '9') {
            if (++digits > 4U) return -22;
            pair[part] = pair[part] * 10U + (uint32_t)(value[at++] - '0');
        }
        if (part == 0U) {
            if (value[at++] != 'x') return -22;
        } else if (value[at] != '\0') return -22;
    }
    if (pair[0] < 800U || pair[1] < 600U ||
        pair[0] > REIST_DISPLAY_MODE_MAX_DIMENSION ||
        pair[1] > REIST_DISPLAY_MODE_MAX_DIMENSION) return -22;
    *width = pair[0]; *height = pair[1];
    return 0;
}

int reist_display_setting_supported(uint32_t width, uint32_t height,
                                    const reist_display_mode_request_t *caps) {
    return reist_display_mode_supported(width, height, caps);
}
