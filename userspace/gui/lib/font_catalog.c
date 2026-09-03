/** @file userspace/gui/lib/font_catalog.c */
#include "reist/gui/font_catalog.h"

#include <stddef.h>

static const uint32_t heights[REIST_GUI_FONT_SIZE_COUNT] = {
    10U, 12U, 14U, 16U, 18U, 20U, 24U, 28U,
};

static const reist_gui_font_catalog_entry_t entries[
    REIST_GUI_FONT_FAMILY_COUNT] = {
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_UNIFONT, "GNU Unifont",
     "/usr/share/fonts/reist-unicode.psf", 8U, 16U, 262144U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_JETBRAINS_MONO, "JetBrains Mono",
     "/usr/share/fonts/reist-jetbrains-mono.psf", 12U, 24U, 128U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_SOURCE_CODE_PRO, "Source Code Pro",
     "/usr/share/fonts/reist-source-code-pro.psf", 12U, 24U, 128U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_IOSEVKA, "Iosevka",
     "/usr/share/fonts/reist-iosevka.psf", 10U, 24U, 128U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_FIRA_CODE, "Fira Code",
     "/usr/share/fonts/reist-fira-code.psf", 12U, 24U, 128U},
};

const reist_gui_font_catalog_entry_t *reist_gui_font_catalog_entry(
    uint32_t family) {
    if (family == 0U || family > REIST_GUI_FONT_FAMILY_COUNT) return NULL;
    return &entries[family - 1U];
}

uint32_t reist_gui_font_catalog_height(uint32_t index) {
    return index < REIST_GUI_FONT_SIZE_COUNT ? heights[index] : 0U;
}

int reist_gui_font_catalog_selection_valid(uint32_t family,
                                           uint32_t pixel_height) {
    if (reist_gui_font_catalog_entry(family) == NULL) return 0;
    for (uint32_t index = 0U; index < REIST_GUI_FONT_SIZE_COUNT; ++index) {
        if (heights[index] == pixel_height) return 1;
    }
    return 0;
}

int reist_gui_font_catalog_metrics(uint32_t family, uint32_t pixel_height,
                                   uint32_t *cell_width,
                                   uint32_t *cell_height) {
    const reist_gui_font_catalog_entry_t *entry =
        reist_gui_font_catalog_entry(family);
    if (entry == NULL || cell_width == NULL || cell_height == NULL ||
        !reist_gui_font_catalog_selection_valid(family, pixel_height))
        return -22;
    uint32_t width = entry->base_width * pixel_height +
        entry->base_height / 2U;
    width /= entry->base_height;
    if (width == 0U) width = 1U;
    if (width > 32U) return -75;
    *cell_width = width;
    *cell_height = pixel_height;
    return 0;
}
