#include "browser_model.h"

static uint32_t utf8_length(const char *text, uint32_t remaining) {
    if (remaining == 0U) return 0U;
    uint8_t first = (uint8_t)text[0U];
    if (first < 0x80U) return 1U;
    if (first >= 0xC2U && first <= 0xDFU && remaining >= 2U) return 2U;
    if (first >= 0xE0U && first <= 0xEFU && remaining >= 3U) return 3U;
    return remaining >= 4U ? 4U : remaining;
}

static uint32_t font_height(uint32_t style) {
    if (style & REIST_HTML_STYLE_HEADING_1) return 24U;
    if (style & REIST_HTML_STYLE_HEADING_2) return 20U;
    if (style & REIST_HTML_STYLE_HEADING_3) return 18U;
    return BROWSER_BODY_FONT;
}

static uint32_t font_cell(uint32_t height) {
    return height > 16U ? (height + 1U) / 2U : 8U;
}

static int add_run(browser_layout_t *layout, uint32_t kind,
                   uint32_t offset, uint32_t length, uint32_t style,
                   uint32_t link_index, int32_t x, uint32_t y,
                   uint32_t width, uint32_t height) {
    if (layout->run_count >= BROWSER_LAYOUT_LINE_CAPACITY) return -28;
    if (kind == REIST_HTML_ELEMENT_TEXT && layout->run_count != 0U) {
        browser_layout_run_t *previous = &layout->runs[layout->run_count - 1U];
        if (previous->kind == kind && previous->style == style &&
            previous->link_index == link_index && previous->y == (int32_t)y &&
            previous->x + (int32_t)previous->width == x &&
            previous->text_offset + previous->text_length == offset &&
            previous->text_length + length <
                40U) {
            previous->text_length += length;
            previous->width += width;
            return 0;
        }
    }
    layout->runs[layout->run_count++] = (browser_layout_run_t){
        kind, offset, length, style, link_index, x, y, width, height};
    return 0;
}

static void next_line(uint32_t *x, uint32_t *y, uint32_t *line_height,
                      uint32_t indent) {
    *y += *line_height;
    *x = 16U + indent;
    *line_height = BROWSER_BODY_FONT + 2U;
}

int browser_build_layout(const reist_html_document_t *document,
                        uint32_t width, const browser_image_slot_t *images,
                        browser_layout_t *layout) {
    layout->run_count = 0U;
    layout->total_height = 0U;
    uint32_t right = width > 52U ? width - BROWSER_SCROLLBAR_WIDTH - 18U : 17U;
    uint32_t x = 16U, y = 4U, line_height = BROWSER_BODY_FONT + 2U;
    uint32_t has_content = 0U;
    for (uint32_t element_index = 0U;
         element_index < document->element_count; ++element_index) {
        const reist_html_element_t *element =
            &document->elements[element_index];
        uint32_t indent = element->list_depth > 8U
            ? 128U : (uint32_t)element->list_depth * 16U;
        if (indent >= right - 16U) indent = 0U;
        if (element->kind == REIST_HTML_ELEMENT_ANCHOR) {
            if (add_run(layout, element->kind, element->text_offset, 0U, 0U,
                        UINT32_MAX, 16, y, 0U, 0U) != 0) return -28;
            continue;
        }
        if (element->kind == REIST_HTML_ELEMENT_IMAGE) {
            uint32_t image_index = element->text_offset;
            if (image_index >= document->image_count) return -22;
            if (x != 16U) next_line(&x, &y, &line_height, 0U);
            const reist_html_image_t *image = &document->images[image_index];
            uint32_t source_w = 160U, source_h = 48U;
            if (images != 0 && image_index < BROWSER_IMAGE_CACHE_COUNT &&
                images[image_index].decoded) {
                source_w = images[image_index].source_width;
                source_h = images[image_index].source_height;
            }
            uint32_t w = image->width ? image->width : source_w;
            uint32_t h = image->height ? image->height : source_h;
            if (image->width && !image->height) h = source_h * w / source_w;
            if (image->height && !image->width) w = source_w * h / source_h;
            if (w > right - 16U) { h = h * (right - 16U) / w; w = right - 16U; }
            if (w == 0U) w = 1U;
            if (h == 0U) h = 1U;
            if (add_run(layout, element->kind, image_index, 0U, element->style,
                        element->link_index, 16, y, w, h) != 0) return -28;
            y += h + 6U;
            has_content = 1U;
            continue;
        }
        if (element->kind == REIST_HTML_ELEMENT_PARAGRAPH_BREAK) {
            if (has_content) {
                next_line(&x, &y, &line_height, 0U);
                y += 7U;
            }
            continue;
        }
        if (element->kind == REIST_HTML_ELEMENT_LINE_BREAK) {
            next_line(&x, &y, &line_height, indent);
            has_content = 1U;
            continue;
        }
        if (element->kind == REIST_HTML_ELEMENT_LIST_MARKER) {
            if (x != 16U) next_line(&x, &y, &line_height, indent);
            x = 16U + indent;
            if (add_run(layout, element->kind, 0U, element->text_length,
                        element->style,
                        UINT32_MAX, (int32_t)x - 20, y, 18U,
                        BROWSER_BODY_FONT) != 0) return -28;
            has_content = 1U;
            continue;
        }
        if (element->kind != REIST_HTML_ELEMENT_TEXT ||
            element->text_offset + element->text_length >
                document->text_length) return -22;
        uint32_t height = font_height(element->style);
        uint32_t cell = font_cell(height);
        if (x == 16U && indent != 0U) x += indent;
        if (height + 2U > line_height) line_height = height + 2U;
        uint32_t consumed = 0U;
        while (consumed < element->text_length) {
            /* Keep words together when they fit; long words still wrap at a
             * complete UTF-8 scalar. All lookahead is within this element. */
            if (!(element->style & REIST_HTML_STYLE_PREFORMATTED) &&
                (consumed == 0U || document->text[element->text_offset + consumed - 1U] == ' ')) {
                uint32_t word = 0U, look = consumed;
                while (look < element->text_length &&
                       document->text[element->text_offset + look] != ' ') {
                    look += utf8_length(document->text + element->text_offset + look,
                                        element->text_length - look);
                    word += cell;
                }
                if (word <= right - 16U - indent && x > 16U + indent &&
                    x + word > right) next_line(&x, &y, &line_height, indent);
            }
            const char *scalar = document->text + element->text_offset + consumed;
            uint32_t scalar_length = utf8_length(
                scalar, element->text_length - consumed);
            if ((element->style & REIST_HTML_STYLE_PREFORMATTED) &&
                scalar_length == 1U && scalar[0U] == '\n') {
                next_line(&x, &y, &line_height, indent);
                consumed += scalar_length;
                continue;
            }
            if (x + cell > right && x > 16U + indent)
                next_line(&x, &y, &line_height, indent);
            if (height + 2U > line_height) line_height = height + 2U;
            if (add_run(layout, element->kind,
                        element->text_offset + consumed, scalar_length,
                        element->style, element->link_index, (int32_t)x, y,
                        cell, height) != 0) return -28;
            x += cell;
            consumed += scalar_length;
            has_content = 1U;
        }
    }
    layout->total_height = y + line_height + 4U;
    return 0;
}


uint32_t browser_point_in_rect(reist_gui_rect_t r, int32_t x, int32_t y) {
    return x >= r.x && y >= r.y && (uint32_t)(x - r.x) < r.width &&
           (uint32_t)(y - r.y) < r.height;
}
int browser_anchor_y(const reist_html_document_t *document,
                      const browser_layout_t *layout, const char *name, uint32_t *y) {
    char decoded[128U]; size_t used = 0U;
    for (size_t i = 0U; name[i]; ++i) {
        if (used + 1U >= sizeof(decoded)) return -28;
        unsigned value = (unsigned char)name[i];
        if (value == '%' && name[i + 1U] && name[i + 2U]) {
            unsigned a = (unsigned char)name[i + 1U], b = (unsigned char)name[i + 2U];
            a = a >= '0' && a <= '9' ? a - '0' : (a | 32U) - 'a' + 10U;
            b = b >= '0' && b <= '9' ? b - '0' : (b | 32U) - 'a' + 10U;
            if (a < 16U && b < 16U) { value = a * 16U + b; i += 2U; }
        }
        if (value == 0U) return -22;
        decoded[used++] = (char)value;
    }
    decoded[used] = '\0';
    if (used == 0U) { *y = 0U; return 0; }
    for (uint32_t i = 0; i < layout->run_count; ++i) {
        const browser_layout_run_t *run = &layout->runs[i];
        if (run->kind != REIST_HTML_ELEMENT_ANCHOR ||
            run->text_offset >= document->anchor_count) continue;
        const char *candidate = document->anchors[run->text_offset].name;
        size_t n = 0U;
        while (n < used && candidate[n] == decoded[n]) ++n;
        if (n == used && candidate[n] == '\0') { *y = run->y>0 ? (uint32_t)run->y : 0; return 0; }
    }
    return -2;
}

int browser_address_edit(char *text, uint32_t capacity, uint32_t *length,
                          uint32_t *cursor, uint32_t *replace, uint32_t key) {
    if (capacity < 2U || *length >= capacity || *cursor > *length) return -22;
    /* Surface keyboard constants: left/right/home/end/delete. */
    if (key == 0x104U || key == 0x105U || key == 0x106U || key == 0x107U) {
        if (key == 0x104U && *cursor) --*cursor;
        if (key == 0x105U && *cursor < *length) ++*cursor;
        if (key == 0x106U) *cursor = 0U;
        if (key == 0x107U) *cursor = *length;
        *replace = 0U; return 1;
    }
    uint32_t erase = key == 8U || key == 127U || key == 0x108U;
    if (!erase && (key < 32U || key > 126U)) return 0;
    if (*replace) { *cursor = *length = *replace = 0U; text[0] = '\0'; }
    if (erase) {
        uint32_t at = *cursor;
        if (key != 0x108U) { if (at == 0U) return 1; --at; --*cursor; }
        else if (at == *length) return 1;
        for (uint32_t i = at; i < *length; ++i) text[i] = text[i + 1U];
        --*length;
    } else {
        if (*length + 1U >= capacity) return 0;
        for (uint32_t i = *length + 1U; i > *cursor; --i) text[i] = text[i - 1U];
        text[(*cursor)++] = (char)key; ++*length;
    }
    return 1;
}

static void scrollbar_geometry(browser_scrollbar_t *bar) {
    uint32_t maximum = (bar->model.flags & REIST_GUI_VALUE_ENABLED)
        ? (uint32_t)bar->model.maximum : 0U;
    uint32_t thumb = bar->track.height;
    if (maximum != 0U)
        thumb = bar->track.height * bar->view / (bar->view + maximum);
    if (thumb < 16U) thumb = 16U;
    if (thumb > bar->track.height) thumb = bar->track.height;
    uint32_t travel = bar->track.height - thumb;
    uint32_t offset = maximum ? (uint32_t)bar->state.value * travel / maximum : 0U;
    bar->thumb = (reist_gui_rect_t){bar->track.x, bar->track.y + (int32_t)offset,
                                   bar->track.width, thumb};
}
void browser_scrollbar_configure(browser_scrollbar_t *bar, uint32_t width,
                                 uint32_t view, uint32_t total, uint32_t position) {
    /* Surface and document capacities bound all products below 2^31. */
    if (view > 768U) view = 768U;
    if (view == 0U) view = 1U;
    if (total > 262144U) total = 262144U;
    bar->view = view;
    bar->bounds = (reist_gui_rect_t){(int32_t)width - (int32_t)BROWSER_SCROLLBAR_WIDTH,
        BROWSER_CONTENT_TOP, BROWSER_SCROLLBAR_WIDTH, view};
    uint32_t arrows = view > 36U ? 18U : 0U;
    bar->track = (reist_gui_rect_t){bar->bounds.x, bar->bounds.y + (int32_t)arrows,
        bar->bounds.width, view - arrows * 2U};
    uint32_t maximum = total > view ? total - view : 0U;
    uint32_t was_captured = bar->state.captured;
    uint32_t was_focused = bar->state.focused;
    reist_gui_range_state_initialize(&bar->state);
    bar->model = (reist_gui_range_model_t){REIST_GUI_VALUE_API_VERSION,
        sizeof(bar->model), 1U, "Browser document scroll", bar->track,
        0, (int32_t)(maximum ? maximum : 1U), 24U, view, REIST_GUI_RANGE_SCROLLBAR,
        REIST_GUI_VERTICAL, REIST_GUI_VALUE_VISIBLE |
            (maximum ? REIST_GUI_VALUE_ENABLED : 0U), {0}};
    reist_gui_value_result_t result;
    reist_gui_value_result_initialize(&result);
    (void)reist_gui_range_configure(&bar->model, &bar->state,
        (int32_t)(position > maximum ? maximum : position), &result);
    bar->state.focused = maximum ? was_focused : 0U;
    bar->state.captured = maximum && was_focused ? was_captured : 0U;
    scrollbar_geometry(bar);
}
int browser_scrollbar_pointer(browser_scrollbar_t *bar, uint32_t motion,
                               uint32_t pressed, int32_t x, int32_t y) {
    if (!(bar->model.flags & REIST_GUI_VALUE_ENABLED)) return 0;
    uint32_t inside = browser_point_in_rect(bar->bounds, x, y);
    if (!bar->state.captured && (motion || !pressed || !inside)) return 0;
    int32_t previous = bar->state.value;
    reist_gui_value_result_t result;
    reist_gui_value_result_initialize(&result);
    if (!motion && pressed && !bar->state.captured) {
        if (browser_point_in_rect(bar->thumb, x, y)) {
            bar->grab = y - bar->thumb.y;
            bar->origin_y = y;
            bar->origin_value = previous;
            bar->state.focused = 1U;
            bar->state.captured = 1U;
            return 1;
        } else {
            int32_t next = previous;
            if (y < bar->track.y) next -= 24;
            else if (y >= bar->track.y + (int32_t)bar->track.height) next += 24;
            else if (y < bar->thumb.y) next -= (int32_t)bar->view;
            else next += (int32_t)bar->view;
            if (next < 0) next = 0;
            if (next > bar->model.maximum) next = bar->model.maximum;
            (void)reist_gui_range_set(&bar->model, &bar->state, next, &result);
            scrollbar_geometry(bar);
            return 1;
        }
    }
    /* Adapt thumb travel to the shared value controller, retaining the exact
     * press position/value. A press or zero motion cannot jump by rounding. */
    int32_t travel = (int32_t)(bar->track.height - bar->thumb.height);
    int64_t delta = (int64_t)y - bar->origin_y;
    if (delta < -travel) delta = -travel;
    if (delta > travel) delta = travel;
    int32_t next = bar->origin_value;
    if (travel > 0) next += (int32_t)delta * bar->model.maximum / travel;
    if (next < 0) next = 0;
    if (next > bar->model.maximum) next = bar->model.maximum;
    (void)reist_gui_range_set(&bar->model, &bar->state, (int32_t)next, &result);
    if (!motion && !pressed) bar->state.captured = 0U;
    scrollbar_geometry(bar);
    return 1;
}
