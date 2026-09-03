/**
 * @file reist/gui/font_catalog.h
 * @brief Fixed selectable bitmap-font catalog shared by Surface clients.
 */
#ifndef REIST_GUI_FONT_CATALOG_H
#define REIST_GUI_FONT_CATALOG_H

#include <stdint.h>

#define REIST_GUI_FONT_CATALOG_API_VERSION 1U
#define REIST_GUI_FONT_FAMILY_COUNT 5U
#define REIST_GUI_FONT_SIZE_COUNT 8U
#define REIST_GUI_FONT_DEFAULT_FAMILY 1U
#define REIST_GUI_FONT_DEFAULT_HEIGHT 16U

enum reist_gui_font_family {
    REIST_GUI_FONT_FAMILY_UNIFONT = 1U,
    REIST_GUI_FONT_FAMILY_JETBRAINS_MONO,
    REIST_GUI_FONT_FAMILY_SOURCE_CODE_PRO,
    REIST_GUI_FONT_FAMILY_IOSEVKA,
    REIST_GUI_FONT_FAMILY_FIRA_CODE
};

typedef struct reist_gui_font_catalog_entry {
    uint32_t version;
    uint32_t struct_size;
    uint32_t id;
    const char *name;
    const char *path;
    uint32_t base_width;
    uint32_t base_height;
    uint32_t mapping_capacity;
} reist_gui_font_catalog_entry_t;

const reist_gui_font_catalog_entry_t *reist_gui_font_catalog_entry(
    uint32_t family);
uint32_t reist_gui_font_catalog_height(uint32_t index);
int reist_gui_font_catalog_selection_valid(uint32_t family,
                                           uint32_t pixel_height);
int reist_gui_font_catalog_metrics(uint32_t family, uint32_t pixel_height,
                                   uint32_t *cell_width,
                                   uint32_t *cell_height);

#endif
