#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include "browser_model.h"
static reist_html_document_t document;
static browser_layout_t layout;
static browser_image_slot_t images[BROWSER_IMAGE_CACHE_COUNT];
static void editing(void) {
    char text[256] = "old";
    uint32_t length = 3, cursor = 3, replace = 1;
    const char *url = "https://example.test/path?q=1";
    for (size_t i = 0; url[i]; ++i)
        assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, url[i]) == 1);
    assert(strcmp(text, url) == 0);
    assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 0x106) == 1);
    assert(cursor == 0);
    assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 'X') == 1);
    assert(text[0] == 'X' && text[1] == 'h');
    assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 8) == 1);
    assert(strcmp(text, url) == 0);
    cursor = 4;
    assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 0x108) == 1);
    assert(strncmp(text, "http://", 7) == 0);
    while (length < 255) assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 'a') == 1);
    assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 'a') == 0);
    assert(text[255] == 0);
    cursor = 256;
    assert(browser_address_edit(text, sizeof(text), &length, &cursor, &replace, 'a') < 0);
}
static void scrolling(void) {
    browser_scrollbar_t bar = {0};
    browser_scrollbar_configure(&bar, 800, 502, 1800, 337);
    assert(reist_gui_range_validate(&bar.model, &bar.state) == 0);
    int32_t x = bar.thumb.x + 4, y = bar.thumb.y + 11;
    assert(browser_scrollbar_pointer(&bar, 0, 1, x, y) == 1);
    assert(bar.state.captured && bar.state.value == 337);
    assert(bar.state.focused && reist_gui_range_validate(&bar.model, &bar.state) == 0);
    assert(browser_scrollbar_pointer(&bar, 1, 1, x, y) == 1);
    assert(bar.state.value == 337);
    assert(browser_scrollbar_pointer(&bar, 1, 1, x, y + 20) == 1);
    assert(bar.state.value > 337);
    browser_scrollbar_configure(&bar, 800, 502, 1800, (uint32_t)bar.state.value);
    assert(bar.state.captured);
    assert(bar.state.focused && reist_gui_range_validate(&bar.model, &bar.state) == 0);
    assert(browser_scrollbar_pointer(&bar, 0, 0, -20, y + 20) == 1);
    assert(!bar.state.captured);
    assert(browser_scrollbar_pointer(&bar, 1, 0, x, y + 50) == 0);
    browser_scrollbar_configure(&bar, 800, 502, 1800, 0);
    assert(browser_scrollbar_pointer(&bar, 0, 1, x, bar.bounds.y + (int32_t)bar.bounds.height - 4) == 1);
    assert(bar.state.value == 24);
    assert(browser_scrollbar_pointer(&bar, 0, 1, x, bar.thumb.y + (int32_t)bar.thumb.height + 5) == 1);
    assert(bar.state.value == 526);
    browser_scrollbar_configure(&bar, 800, 502, 100, 0);
    assert(bar.state.value == 0 && bar.thumb.height == bar.track.height);
    assert(reist_gui_range_validate(&bar.model, &bar.state) == 0);
    assert(!(bar.model.flags & REIST_GUI_VALUE_ENABLED));
    assert(browser_scrollbar_pointer(&bar, 0, 1, bar.thumb.x, bar.thumb.y) == 0);
    assert(!bar.state.captured && !bar.state.focused);
    browser_scrollbar_configure(&bar, 40, 1, 200, 199);
    assert(browser_scrollbar_pointer(&bar, 0, 1, bar.thumb.x, bar.thumb.y) == 1);
    assert(browser_scrollbar_pointer(&bar, 0, 0, -1, -1) == 1);
}
static void layout_cases(void) {
    const char *html = "<h1 id='top'>Test</h1><a href='next?x=1&amp;y=2'><img src='p.png' width='120' alt='An &amp; image'></a>"
        "<p>one two three four</p><h2 id='target'>Here</h2><script><img src='bad'></script>";
    assert(reist_html_document_parse((const uint8_t *)html, strlen(html), &document) == 0);
    assert(document.image_count == 1 && document.anchor_count == 2);
    assert(strcmp(document.images[0].source, "p.png") == 0);
    assert(strcmp(document.images[0].alt, "An & image") == 0);
    assert(strcmp(document.links[0].href, "next?x=1&y=2") == 0);
    images[0].decoded = 1; images[0].source_width = 200; images[0].source_height = 100;
    assert(browser_build_layout(&document, 800, images, &layout) == 0);
    uint32_t image_count = 0;
    for (uint32_t i = 0; i < layout.run_count; ++i) {
        browser_layout_run_t *run = &layout.runs[i];
        if (run->kind == REIST_HTML_ELEMENT_IMAGE) {
            ++image_count;
            assert(run->width == 120 && run->height == 60 && run->link_index == 0);
        }
        if (run->kind == REIST_HTML_ELEMENT_TEXT) assert(run->text_length < 40);
    }
    assert(image_count == 1);
    uint32_t y;
    assert(browser_anchor_y(&document, &layout, "t%61rget", &y) == 0 && y > 60);
    assert(browser_anchor_y(&document, &layout, "missing", &y) < 0);
    assert(browser_anchor_y(&document, &layout, "", &y) == 0 && y == 0);
    assert(browser_build_layout(&document, 40, images, &layout) == 0);
    for (uint32_t i = 0; i < layout.run_count; ++i) assert(layout.runs[i].width <= 24);
}
int main(void) { editing(); scrolling(); layout_cases(); return 0; }
