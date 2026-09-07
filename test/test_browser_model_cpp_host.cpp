#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "browser_model.hpp"
extern "C" {
int fixture_main(void);
uint32_t baseline_browser_point_in_rect(reist_gui_rect_t, int32_t, int32_t);
int baseline_browser_build_layout(const reist_html_document_t*, uint32_t, const browser_image_slot_t*, browser_layout_t*);
int baseline_browser_anchor_y(const reist_html_document_t*, const browser_layout_t*, const char*, uint32_t*);
int baseline_browser_address_edit(char*, uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t);
void baseline_browser_scrollbar_configure(browser_scrollbar_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int baseline_browser_scrollbar_pointer(browser_scrollbar_t*, uint32_t, uint32_t, int32_t, int32_t);
}
static unsigned checks;
static uint32_t random_state = 0xabc123U;
static uint32_t random_value() { random_state = random_state * 1664525U + 1013904223U; return random_state; }
static void address_cases() {
    const uint32_t capacities[] = {2U, 3U, 16U, 256U};
    for (uint32_t capacity : capacities) {
        char a[256] = {}, b[256] = {};
        uint32_t al = 0, bl = 0, ac = 0, bc = 0, ar = 1, br = 1;
        for (unsigned i = 0; i < 10000; ++i) {
            const uint32_t keys[] = {'x', ':', '/', '?', 8, 127, 0x104, 0x105, 0x106, 0x107, 0x108, 0, 0x10ffff};
            uint32_t key = keys[random_value() % (sizeof(keys)/sizeof(*keys))];
            if (i % 137 == 0) ar = br = 1;
            assert(browser_address_edit(a, capacity, &al, &ac, &ar, key) ==
                   baseline_browser_address_edit(b, capacity, &bl, &bc, &br, key));
            assert(al == bl && ac == bc && ar == br && !memcmp(a, b, sizeof(a))); ++checks;
        }
        al = bl = capacity; ac = bc = capacity;
        assert(browser_address_edit(a, capacity, &al, &ac, &ar, 'x') == -22);
        assert(baseline_browser_address_edit(b, capacity, &bl, &bc, &br, 'x') == -22);
        assert(!memcmp(a,b,sizeof(a)) && al == bl && ac == bc); ++checks;
    }
    using reist::browser::AddressEdit;
    char data[4] = "abc"; uint32_t length = 3, cursor = 4, replace = 1;
    auto bad = AddressEdit::open(data,4,&length,&cursor,&replace);
    assert(!bad && *bad.error_if() == -22 && !strcmp(data,"abc"));
    assert(!AddressEdit::open(nullptr,4,&length,&cursor,&replace));
    assert(!AddressEdit::open(data,4,nullptr,&cursor,&replace));
    cursor = 3;
    auto good = AddressEdit::open(data,4,&length,&cursor,&replace);
    assert(good && good.value_if()->edit('z') == 1 && !strcmp(data,"z"));
}
static void same_bar(browser_scrollbar_t a, browser_scrollbar_t b) {
    assert(!strcmp(a.model.name, b.model.name));
    // Host pointer alignment introduces unnamed padding, not model state.
    assert(a.model.version == b.model.version && a.model.struct_size == b.model.struct_size && a.model.id == b.model.id);
    assert(!memcmp(&a.model.bounds,&b.model.bounds,sizeof(a.model.bounds)));
    assert(a.model.minimum == b.model.minimum && a.model.maximum == b.model.maximum);
    assert(a.model.step == b.model.step && a.model.page_step == b.model.page_step);
    assert(a.model.role == b.model.role && a.model.orientation == b.model.orientation && a.model.flags == b.model.flags);
    assert(!memcmp(a.model.reserved,b.model.reserved,sizeof(a.model.reserved)));
    assert(!memcmp(&a.state,&b.state,sizeof(a.state)));
    assert(!memcmp(&a.bounds,&b.bounds,sizeof(a.bounds)));
    assert(!memcmp(&a.track,&b.track,sizeof(a.track)));
    assert(!memcmp(&a.thumb,&b.thumb,sizeof(a.thumb)));
    assert(a.grab == b.grab && a.origin_y == b.origin_y && a.origin_value == b.origin_value && a.view == b.view); ++checks;
}
static void scroll_cases() {
    const uint32_t views[] = {0,1,35,36,37,502,768,769,UINT32_MAX};
    const uint32_t totals[] = {0,1,36,1800,262144,UINT32_MAX};
    for (uint32_t view : views) for (uint32_t total : totals) {
        browser_scrollbar_t a = {}, b = {};
        uint32_t position = total / 3;
        browser_scrollbar_configure(&a,800,view,total,position);
        baseline_browser_scrollbar_configure(&b,800,view,total,position); same_bar(a,b);
        for (unsigned i = 0; i < 256; ++i) {
            uint32_t motion = i % 3 == 1, pressed = i % 3 != 2;
            int32_t x = a.thumb.x + 4, y = a.thumb.y + (int32_t)(random_value()%100) - 20;
            assert(browser_scrollbar_pointer(&a,motion,pressed,x,y) ==
                   baseline_browser_scrollbar_pointer(&b,motion,pressed,x,y)); same_bar(a,b);
            assert(browser_point_in_rect(a.bounds,x,y) == baseline_browser_point_in_rect(b.bounds,x,y));
        }
    }
}
static reist_html_document_t document;
static browser_layout_t a, b;
static browser_image_slot_t images[BROWSER_IMAGE_CACHE_COUNT];
static void layout_cases() {
    const char* fixtures[] = {"", "<h1 id='top'>Test</h1><p>one two three four</p>",
        "<pre>a\n\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80</pre><ul><li>a</li><li>b</li></ul>",
        "<a href='x'><img src='p' width='120'></a><h2 id='target'>Here</h2>"};
    const uint32_t widths[] = {0,17,40,52,53,128,800,1024};
    const char* fragments[] = {"", "top", "t%6fp", "target", "missing", "%00", "%GG"};
    images[0].decoded = 1; images[0].source_width = 200; images[0].source_height = 100;
    for (auto html : fixtures) {
        document = {};
        int parsed = reist_html_document_parse((const uint8_t*)html,strlen(html),&document);
        assert(parsed == (html[0] ? 0 : REIST_HTML_INVALID));
        for (uint32_t width : widths) {
            memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
            assert(browser_build_layout(&document,width,images,&a) == baseline_browser_build_layout(&document,width,images,&b));
            assert(!memcmp(&a,&b,sizeof(a))); ++checks;
            for (auto name : fragments) {
                uint32_t ay = 123, by = 123;
                assert(browser_anchor_y(&document,&a,name,&ay) == baseline_browser_anchor_y(&document,&b,name,&by));
                assert(ay == by); ++checks;
            }
        }
    }
    document = {}; document.element_count = 1;
    document.elements[0].kind = REIST_HTML_ELEMENT_TEXT;
    document.elements[0].text_offset = 1; document.elements[0].text_length = 1;
    assert(browser_build_layout(&document,800,images,&a) == baseline_browser_build_layout(&document,800,images,&b));
    using reist::browser::TextRange;
    assert(!TextRange::open(UINT32_MAX,2,16));
    auto end = TextRange::open(16,0,16); assert(end && end.value_if()->offset() == 16);
    assert(!TextRange::open(17,0,16));
}
int main() {
    assert(fixture_main() == 0); address_cases(); scroll_cases(); layout_cases();
    printf("BROWSER_MODEL_CPP_OK checks=%u\n", checks); return 0;
}
