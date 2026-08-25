#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "reist/gui/font.h"

static uint8_t font_bytes[16384];
static reist_gui_font_mapping_t mappings[512];
static uint32_t pixels[32U * 32U];

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file != NULL);
    size_t size = fread(font_bytes, 1U, sizeof(font_bytes), file);
    assert(size != 0U && size < sizeof(font_bytes));
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);

    reist_gui_font_t font = {0};
    assert(reist_gui_font_open_psf2(
        &font, font_bytes, size, mappings, 512U, 0x25A0U) == 0);
    assert(font.width == 8U && font.height == 16U);
    assert(font.glyph_count == 257U && font.mapping_count == 289U);
    uint32_t glyph = 0U;
    assert(reist_gui_font_lookup(&font, 'A', &glyph) == 1 && glyph == 0x41U);
    assert(reist_gui_font_lookup(&font, 0x03B1U, &glyph) == 1 && glyph == 0xE0U);
    assert(reist_gui_font_lookup(&font, 0x20ACU, &glyph) == 1 && glyph == 256U);
    assert(reist_gui_font_lookup(&font, 0x1F680U, &glyph) == 0 && glyph == 0xFEU);
    assert(reist_gui_font_raster_xrgb(
        &font, 256U, 0x00FFFFFFU, 0x00000000U,
        pixels, 32U, 32U * 32U) == 0);
    uint32_t foreground = 0U;
    uint32_t background = 0U;
    for (uint32_t y = 0U; y < font.height; ++y) {
        for (uint32_t x = 0U; x < font.width; ++x) {
            foreground += pixels[y * 32U + x] == 0x00FFFFFFU;
            background += pixels[y * 32U + x] == 0x00000000U;
        }
    }
    assert(foreground != 0U && background != 0U &&
           foreground + background == 8U * 16U);

    reist_gui_font_t unchanged = {.version = 77U};
    assert(reist_gui_font_open_psf2(
        &unchanged, font_bytes, size, mappings, 8U, 0x25A0U) == -28);
    assert(unchanged.version == 77U);
    uint8_t saved = font_bytes[0];
    font_bytes[0] ^= 0xFFU;
    assert(reist_gui_font_open_psf2(
        &unchanged, font_bytes, size, mappings, 512U, 0x25A0U) == -84);
    assert(unchanged.version == 77U);
    font_bytes[0] = saved;
    assert(reist_gui_font_open_psf2(
        &unchanged, font_bytes, size - 1U, mappings, 512U, 0x25A0U) == -84);
    return 0;
}
