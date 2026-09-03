/** @file userspace/gui/lib/font_catalog.c */
#include "reist/gui/font_catalog.h"

#include <stddef.h>

static const uint32_t heights[REIST_GUI_FONT_SIZE_COUNT] = {
    10U, 12U, 14U, 16U, 18U, 20U, 24U, 28U,
};

static const reist_gui_font_catalog_asset_t outline_assets[
        REIST_GUI_FONT_FAMILY_COUNT - 1U][REIST_GUI_FONT_SIZE_COUNT] = {
    {
        {"/usr/share/fonts/reist-jetbrains-mono-10.psf", 5U, 10U},
        {"/usr/share/fonts/reist-jetbrains-mono-12.psf", 7U, 12U},
        {"/usr/share/fonts/reist-jetbrains-mono-14.psf", 7U, 14U},
        {"/usr/share/fonts/reist-jetbrains-mono-16.psf", 8U, 16U},
        {"/usr/share/fonts/reist-jetbrains-mono-18.psf", 10U, 18U},
        {"/usr/share/fonts/reist-jetbrains-mono-20.psf", 11U, 20U},
        {"/usr/share/fonts/reist-jetbrains-mono.psf", 14U, 24U},
        {"/usr/share/fonts/reist-jetbrains-mono-28.psf", 16U, 28U},
    },
    {
        {"/usr/share/fonts/reist-source-code-pro-10.psf", 5U, 10U},
        {"/usr/share/fonts/reist-source-code-pro-12.psf", 7U, 12U},
        {"/usr/share/fonts/reist-source-code-pro-14.psf", 8U, 14U},
        {"/usr/share/fonts/reist-source-code-pro-16.psf", 9U, 16U},
        {"/usr/share/fonts/reist-source-code-pro-18.psf", 11U, 18U},
        {"/usr/share/fonts/reist-source-code-pro-20.psf", 11U, 20U},
        {"/usr/share/fonts/reist-source-code-pro.psf", 14U, 24U},
        {"/usr/share/fonts/reist-source-code-pro-28.psf", 17U, 28U},
    },
    {
        {"/usr/share/fonts/reist-iosevka-10.psf", 5U, 10U},
        {"/usr/share/fonts/reist-iosevka-12.psf", 5U, 12U},
        {"/usr/share/fonts/reist-iosevka-14.psf", 7U, 14U},
        {"/usr/share/fonts/reist-iosevka-16.psf", 8U, 16U},
        {"/usr/share/fonts/reist-iosevka-18.psf", 8U, 18U},
        {"/usr/share/fonts/reist-iosevka-20.psf", 10U, 20U},
        {"/usr/share/fonts/reist-iosevka.psf", 11U, 24U},
        {"/usr/share/fonts/reist-iosevka-28.psf", 13U, 28U},
    },
    {
        {"/usr/share/fonts/reist-fira-code-10.psf", 6U, 10U},
        {"/usr/share/fonts/reist-fira-code-12.psf", 7U, 12U},
        {"/usr/share/fonts/reist-fira-code-14.psf", 8U, 14U},
        {"/usr/share/fonts/reist-fira-code-16.psf", 9U, 16U},
        {"/usr/share/fonts/reist-fira-code-18.psf", 9U, 18U},
        {"/usr/share/fonts/reist-fira-code-20.psf", 11U, 20U},
        {"/usr/share/fonts/reist-fira-code.psf", 14U, 24U},
        {"/usr/share/fonts/reist-fira-code-28.psf", 15U, 28U},
    },
};

static const reist_gui_font_catalog_entry_t entries[
    REIST_GUI_FONT_FAMILY_COUNT] = {
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_UNIFONT, "GNU Unifont",
     "/usr/share/fonts/reist-unicode.psf", 8U, 16U, 262144U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_JETBRAINS_MONO, "JetBrains Mono",
     "/usr/share/fonts/reist-jetbrains-mono.psf", 14U, 24U, 128U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_SOURCE_CODE_PRO, "Source Code Pro",
     "/usr/share/fonts/reist-source-code-pro.psf", 14U, 24U, 128U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_IOSEVKA, "Iosevka",
     "/usr/share/fonts/reist-iosevka.psf", 11U, 24U, 128U},
    {REIST_GUI_FONT_CATALOG_API_VERSION, sizeof(reist_gui_font_catalog_entry_t),
     REIST_GUI_FONT_FAMILY_FIRA_CODE, "Fira Code",
     "/usr/share/fonts/reist-fira-code.psf", 14U, 24U, 128U},
};

const reist_gui_font_catalog_entry_t *reist_gui_font_catalog_entry(
    uint32_t family) {
    if (family == 0U || family > REIST_GUI_FONT_FAMILY_COUNT) return NULL;
    return &entries[family - 1U];
}

uint32_t reist_gui_font_catalog_height(uint32_t index) {
    return index < REIST_GUI_FONT_SIZE_COUNT ? heights[index] : 0U;
}

int reist_gui_font_catalog_size_index(uint32_t pixel_height,
                                      uint32_t *size_index) {
    if (size_index == NULL) return -22;
    for (uint32_t index = 0U; index < REIST_GUI_FONT_SIZE_COUNT; ++index) {
        if (heights[index] != pixel_height) continue;
        *size_index = index;
        return 0;
    }
    return -22;
}

const reist_gui_font_catalog_asset_t *reist_gui_font_catalog_asset(
        uint32_t family, uint32_t pixel_height) {
    uint32_t size_index = 0U;
    if (family < REIST_GUI_FONT_FAMILY_JETBRAINS_MONO ||
        family > REIST_GUI_FONT_FAMILY_FIRA_CODE ||
        reist_gui_font_catalog_size_index(pixel_height, &size_index) != 0)
        return NULL;
    return &outline_assets[
        family - REIST_GUI_FONT_FAMILY_JETBRAINS_MONO][size_index];
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
    const reist_gui_font_catalog_asset_t *asset =
        reist_gui_font_catalog_asset(family, pixel_height);
    uint32_t width = 0U;
    if (asset != NULL) {
        width = asset->cell_width;
    } else {
        width = entry->base_width * pixel_height +
            entry->base_height / 2U;
        width /= entry->base_height;
    }
    if (width == 0U) width = 1U;
    if (width > 32U) return -75;
    *cell_width = width;
    *cell_height = pixel_height;
    return 0;
}
