#include <assert.h>
#include <stdint.h>

#include "reist/gui/font_catalog.h"

int main(void) {
    static const uint32_t heights[REIST_GUI_FONT_SIZE_COUNT] = {
        10U, 12U, 14U, 16U, 18U, 20U, 24U, 28U,
    };
    assert(reist_gui_font_catalog_entry(0U) == 0);
    assert(reist_gui_font_catalog_entry(6U) == 0);
    for (uint32_t family = 1U;
         family <= REIST_GUI_FONT_FAMILY_COUNT; ++family) {
        const reist_gui_font_catalog_entry_t *entry =
            reist_gui_font_catalog_entry(family);
        assert(entry != 0);
        assert(entry->version == REIST_GUI_FONT_CATALOG_API_VERSION);
        assert(entry->struct_size == sizeof(*entry));
        assert(entry->id == family);
        assert(entry->name != 0 && entry->name[0] != '\0');
        assert(entry->path != 0 && entry->path[0] == '/');
        for (uint32_t index = 0U;
             index < REIST_GUI_FONT_SIZE_COUNT; ++index) {
            uint32_t width = 0U;
            uint32_t height = 0U;
            assert(reist_gui_font_catalog_height(index) == heights[index]);
            assert(reist_gui_font_catalog_selection_valid(
                family, heights[index]));
            assert(reist_gui_font_catalog_metrics(
                family, heights[index], &width, &height) == 0);
            assert(width > 0U && width <= 32U && height == heights[index]);
        }
    }
    assert(reist_gui_font_catalog_height(REIST_GUI_FONT_SIZE_COUNT) == 0U);
    assert(!reist_gui_font_catalog_selection_valid(1U, 11U));
    assert(!reist_gui_font_catalog_selection_valid(99U, 16U));
    return 0;
}
