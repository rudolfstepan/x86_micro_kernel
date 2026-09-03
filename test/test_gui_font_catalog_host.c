#include <assert.h>
#include <stdint.h>

#include "reist/gui/font_catalog.h"

int main(void) {
    static const uint32_t heights[REIST_GUI_FONT_SIZE_COUNT] = {
        10U, 12U, 14U, 16U, 18U, 20U, 24U, 28U,
    };
    static const uint32_t widths[REIST_GUI_FONT_FAMILY_COUNT - 1U]
                                 [REIST_GUI_FONT_SIZE_COUNT] = {
        {5U, 7U, 7U, 8U, 10U, 11U, 14U, 16U},
        {5U, 7U, 8U, 9U, 11U, 11U, 14U, 17U},
        {5U, 5U, 7U, 8U, 8U, 10U, 11U, 13U},
        {6U, 7U, 8U, 9U, 9U, 11U, 14U, 15U},
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
            uint32_t size_index = UINT32_MAX;
            assert(reist_gui_font_catalog_size_index(
                heights[index], &size_index) == 0);
            assert(size_index == index);
            assert(reist_gui_font_catalog_selection_valid(
                family, heights[index]));
            assert(reist_gui_font_catalog_metrics(
                family, heights[index], &width, &height) == 0);
            assert(width > 0U && width <= 32U && height == heights[index]);
            const reist_gui_font_catalog_asset_t *asset =
                reist_gui_font_catalog_asset(family, heights[index]);
            if (family == REIST_GUI_FONT_FAMILY_UNIFONT) {
                assert(asset == 0);
            } else {
                assert(asset != 0 && asset->path != 0 &&
                       asset->path[0] == '/');
                assert(asset->cell_width == widths[family - 2U][index]);
                assert(asset->cell_height == heights[index]);
                assert(width == asset->cell_width);
            }
        }
    }
    assert(reist_gui_font_catalog_height(REIST_GUI_FONT_SIZE_COUNT) == 0U);
    assert(!reist_gui_font_catalog_selection_valid(1U, 11U));
    assert(!reist_gui_font_catalog_selection_valid(99U, 16U));
    uint32_t unchanged = 77U;
    assert(reist_gui_font_catalog_size_index(11U, &unchanged) == -22);
    assert(unchanged == 77U);
    assert(reist_gui_font_catalog_size_index(16U, 0) == -22);
    assert(reist_gui_font_catalog_asset(1U, 16U) == 0);
    assert(reist_gui_font_catalog_asset(2U, 11U) == 0);
    return 0;
}
