#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "include/reist/utf.h"
#include "include/reist/unicode_vga_font.h"

int main(void) {
    static const char run[] =
        "A\xC3\xA4\xE2\x82\xAC\xF0\x9F\x9A\x80";
    size_t scalars = 0U;
    assert(reist_utf8_scan(run, sizeof(run) - 1U, &scalars));
    assert(scalars == 4U);

    size_t prefix_bytes = 99U;
    size_t prefix_scalars = 99U;
    assert(reist_utf8_prefix(run, sizeof(run) - 1U, 2U,
                             &prefix_bytes, &prefix_scalars));
    assert(prefix_bytes == 3U);
    assert(prefix_scalars == 2U);

    static const char maximum[] = "\xF4\x8F\xBF\xBF";
    assert(reist_utf8_scan(maximum, sizeof(maximum) - 1U, &scalars));
    assert(scalars == 1U);

    static const char overlong[] = "\xC0\xAF";
    static const char surrogate[] = "\xED\xA0\x80";
    static const char truncated[] = "\xF0\x9F\x9A";
    static const char above_maximum[] = "\xF4\x90\x80\x80";
    assert(!reist_utf8_scan(overlong, sizeof(overlong) - 1U, &scalars));
    assert(!reist_utf8_scan(surrogate, sizeof(surrogate) - 1U, &scalars));
    assert(!reist_utf8_scan(truncated, sizeof(truncated) - 1U, &scalars));
    assert(!reist_utf8_scan(
        above_maximum, sizeof(above_maximum) - 1U, &scalars));
    prefix_bytes = 77U;
    prefix_scalars = 88U;
    assert(!reist_utf8_prefix(truncated, sizeof(truncated) - 1U, 1U,
                              &prefix_bytes, &prefix_scalars));
    assert(prefix_bytes == 77U && prefix_scalars == 88U);

    assert(reist_unicode_vga_glyph('A') == 0x41U);
    assert(reist_unicode_vga_glyph(0x00C4U) == 0x8EU);
    assert(reist_unicode_vga_glyph(0x03B1U) == 0xE0U);
    assert(reist_unicode_vga_glyph(0x263AU) == 0x01U);
    assert(reist_unicode_vga_glyph(0x25A0U) == 0xFEU);
    assert(reist_unicode_vga_glyph(0x1F680U) ==
           REIST_UNICODE_VGA_MISSING_GLYPH);
    return 0;
}
