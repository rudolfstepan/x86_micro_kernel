#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "reist/display_settings.h"

int main(void) {
    uint32_t width = 99, height = 99;
    assert(reist_display_setting_parse(NULL, &width, &height) == 0 && !width && !height);
    assert(reist_display_setting_parse("auto", &width, &height) == 0 && !width && !height);
    assert(reist_display_setting_parse("1920x1080", &width, &height) == 0);
    assert(width == 1920 && height == 1080);
    const char *invalid[] = {"", "1024", "1024X768", "0x0", "0800x600", "800x0600",
        "800x600 ", " 800x600", "800x600x32", "800x-600", "-800x600", "auto\n",
        "4294967296x600", "4294967295x600", "800x4294967296", "1x1", "800x0"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        width = height = 77;
        assert(reist_display_setting_parse(invalid[i], &width, &height) != 0);
        assert(width == 0 && height == 0);
    }
    assert(reist_display_setting_parse("auto", NULL, &height) != 0);
    reist_display_mode_request_t caps = {0};
    caps.version = REIST_DISPLAY_MODE_VERSION;
    caps.struct_size = sizeof(caps);
    caps.operation = REIST_DISPLAY_MODE_QUERY;
    caps.backend = REIST_DISPLAY_BACKEND_SVGA2;
    caps.max_width = 2560; caps.max_height = 1600;
    caps.scanout_bytes = caps.shadow_bytes = 16U*1024U*1024U;
    caps.bpp = 32;
    assert(reist_display_setting_supported(1920, 1080, &caps));
    assert(reist_display_setting_supported(2560, 1600, &caps));
    assert(!reist_display_setting_supported(UINT32_MAX, UINT32_MAX, &caps));
    caps.shadow_bytes = 1024*768*4;
    assert(!reist_display_setting_supported(1920, 1080, &caps));
    assert(reist_display_setting_supported(800, 600, &caps));
    caps.backend = REIST_DISPLAY_BACKEND_VBE;
    caps.fixed_width = 1024; caps.fixed_height = 768;
    assert(!reist_display_setting_supported(800, 600, &caps));
    assert(reist_display_setting_supported(1024, 768, &caps));
    caps.reserved2 = 1;
    assert(!reist_display_setting_supported(1024, 768, &caps));
    puts("DISPLAY_TEST_OK parser mode-limits fixed-vbe overflow fail-closed");
    return 0;
}
