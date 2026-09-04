/**
 * @file main.c
 * @brief Bounded semantic HTML browser implemented as a Ring-3 Surface client.
 *
 * Network and TLS authority stay in the separately restartable CURL.PRG child.
 * This process owns only a Surface, one completed VFS object at a time and its
 * fixed-capacity renderer-neutral document/layout state.  Script elements are
 * inert; a future JavaScript engine must cross a versioned service boundary.
 */
#include "x86os.h"
#include "reist/gui/font_catalog.h"
#include "reist/gui/html_document.h"
#include "reist/gui/surface_client.h"
#include "reist/vfs_file_client.h"

#include <stddef.h>
#include <stdint.h>

#define BROWSER_DOCUMENT_LIMIT 65536U
#define BROWSER_URL_CAPACITY 256U
#define BROWSER_LAYOUT_LINE_CAPACITY 2048U
#define BROWSER_LINK_HIT_CAPACITY 128U
#define BROWSER_READ_CHUNK 4096U
#define BROWSER_CREATE_ATTEMPTS 250U
#define BROWSER_PAINT_ATTEMPTS 20U
#define BROWSER_VISIBLE_RUN_BUDGET 150U
#define BROWSER_CONTENT_TOP 76U
#define BROWSER_STATUS_HEIGHT 22U
#define BROWSER_BODY_FONT 16U
#define BROWSER_KEY_ESCAPE 0x101U
#define BROWSER_KEY_UP 0x102U
#define BROWSER_KEY_DOWN 0x103U
#define BROWSER_KEY_HOME 0x106U
#define BROWSER_KEY_END 0x107U
#define BROWSER_KEY_PAGE_UP 0x109U
#define BROWSER_KEY_PAGE_DOWN 0x10AU

typedef struct browser_layout_run {
    uint32_t kind;
    uint32_t text_offset;
    uint32_t text_length;
    uint32_t style;
    uint32_t link_index;
    int32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} browser_layout_run_t;

typedef struct browser_layout {
    browser_layout_run_t runs[BROWSER_LAYOUT_LINE_CAPACITY];
    uint32_t run_count;
    uint32_t total_height;
} browser_layout_t;

typedef struct browser_link_hit {
    reist_gui_rect_t rect;
    uint32_t link_index;
} browser_link_hit_t;

typedef struct browser_state {
    uint32_t active;
    uint32_t loaded;
    uint32_t redraw;
    uint32_t exit_requested;
    uint32_t address_focused;
    uint32_t address_replace_pending;
    uint32_t address_length;
    uint32_t scroll_y;
    uint32_t probe;
    uint32_t probe_phase;
    char address[BROWSER_URL_CAPACITY];
    char active_url[BROWSER_URL_CAPACITY];
    char temporary_path[40U];
    char status[64U];
    browser_link_hit_t hits[BROWSER_LINK_HIT_CAPACITY];
    uint32_t hit_count;
} browser_state_t;

static uint8_t document_bytes[BROWSER_DOCUMENT_LIMIT];
static reist_html_document_t documents[2U];
static browser_layout_t layouts[2U];

static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0U;
    if (text == 0) return capacity;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

static int text_prefix(const char *text, const char *prefix) {
    size_t index = 0U;
    if (text == 0 || prefix == 0) return 0;
    while (prefix[index] != '\0') {
        char value = text[index];
        if (value >= 'A' && value <= 'Z') value += (char)('a' - 'A');
        if (value != prefix[index]) return 0;
        ++index;
    }
    return 1;
}

static int text_equal(const char *left, const char *right) {
    size_t index = 0U;
    if (left == 0 || right == 0) return 0;
    while (index < BROWSER_URL_CAPACITY && left[index] != '\0' &&
           right[index] != '\0') {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return index < BROWSER_URL_CAPACITY && left[index] == right[index];
}

static int copy_text(char *target, size_t capacity, const char *source) {
    size_t length = bounded_length(source, capacity);
    if (length >= capacity) return -28;
    for (size_t index = 0U; index <= length; ++index)
        target[index] = source[index];
    return 0;
}

static void set_status(browser_state_t *state, const char *status) {
    if (copy_text(state->status, sizeof(state->status), status) != 0)
        state->status[0U] = '\0';
    state->redraw = 1U;
}

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
            previous->link_index == link_index && previous->y == y &&
            previous->x + (int32_t)previous->width == x &&
            previous->text_offset + previous->text_length == offset &&
            previous->text_length + length <
                REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY) {
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

static int build_layout(const reist_html_document_t *document,
                        uint32_t width, browser_layout_t *layout) {
    layout->run_count = 0U;
    layout->total_height = 0U;
    uint32_t right = width > 36U ? width - 20U : 17U;
    uint32_t x = 16U, y = 4U, line_height = BROWSER_BODY_FONT + 2U;
    uint32_t has_content = 0U;
    for (uint32_t element_index = 0U;
         element_index < document->element_count; ++element_index) {
        const reist_html_element_t *element =
            &document->elements[element_index];
        uint32_t indent = element->list_depth > 8U
            ? 128U : (uint32_t)element->list_depth * 16U;
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

static int make_temporary_path(browser_state_t *state) {
    static const char prefix[] = "/browser-";
    static const char suffix[] = ".tmp";
    uint32_t used = 0U;
    for (uint32_t index = 0U; prefix[index] != '\0'; ++index)
        state->temporary_path[used++] = prefix[index];
    uint32_t pid = (uint32_t)x86os_getpid();
    char digits[10U]; uint32_t count = 0U;
    do { digits[count++] = (char)('0' + pid % 10U); pid /= 10U; }
    while (pid != 0U && count < sizeof(digits));
    while (count != 0U) state->temporary_path[used++] = digits[--count];
    for (uint32_t index = 0U; suffix[index] != '\0'; ++index)
        state->temporary_path[used++] = suffix[index];
    state->temporary_path[used] = '\0';
    return 0;
}

static int strip_fragment(const char *url, char *target, size_t capacity) {
    size_t used = 0U;
    while (url[used] != '\0' && url[used] != '#') {
        if (used + 1U >= capacity) return -28;
        target[used] = url[used];
        ++used;
    }
    target[used] = '\0';
    return used == 0U ? -22 : 0;
}

static int fetch_network(browser_state_t *state, const char *url) {
    (void)x86os_unlink(state->temporary_path);
    const char *arguments[] = {
        "/usr/bin/curl.prg", "-o", state->temporary_path,
        "--max-bytes", "65536", url};
    int pid = x86os_spawnv("/usr/bin/curl.prg", 6, arguments);
    int child_status = 1;
    if (pid < 0 || x86os_wait(pid, &child_status) != pid || child_status != 0) {
        (void)x86os_unlink(state->temporary_path);
        return -5;
    }
    return 0;
}

static int read_document(const char *path, uint32_t *length) {
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int status = reist_vfs_file_open_rights(
        path, REIST_VFS_FILE_DEFAULT_TIMEOUT_MS,
        REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT, &handle);
    if (status != 0) return status;
    x86os_file_info_t info;
    status = reist_vfs_file_fstat(handle, &info);
    if (status == 0 && (info.type != X86OS_FILE || info.size == 0U ||
                        info.size > BROWSER_DOCUMENT_LIMIT)) status = -27;
    uint32_t used = 0U;
    while (status == 0 && used < info.size) {
        uint32_t amount = info.size - used;
        if (amount > BROWSER_READ_CHUNK) amount = BROWSER_READ_CHUNK;
        int count = reist_vfs_file_read_bulk(
            handle, document_bytes + used, amount);
        if (count <= 0 || (uint32_t)count > amount) {
            status = count < 0 ? count : -5;
            break;
        }
        used += (uint32_t)count;
    }
    int close_status = reist_vfs_file_close(handle);
    if (status == 0 && close_status != 0) status = close_status;
    if (status == 0 && used != info.size) status = -5;
    if (status == 0) *length = used;
    return status;
}

static int load_candidate(browser_state_t *state,
                          reist_gui_surface_client_t *client,
                          const char *target) {
    char fetch_target[BROWSER_URL_CAPACITY];
    if (strip_fragment(target, fetch_target, sizeof(fetch_target)) != 0)
        return -22;
    uint8_t network = text_prefix(fetch_target, "http://") ||
                      text_prefix(fetch_target, "https://");
    int status = network ? fetch_network(state, fetch_target) : 0;
    const char *path = network ? state->temporary_path : fetch_target;
    uint32_t length = 0U;
    if (status == 0) status = read_document(path, &length);
    uint32_t candidate = state->active ^ 1U;
    if (status == 0)
        status = reist_html_document_parse(
            document_bytes, length, &documents[candidate]);
    if (status == 0)
        status = build_layout(
            &documents[candidate], client->width, &layouts[candidate]);
    if (network) (void)x86os_unlink(state->temporary_path);
    if (status != 0) return status;
    if (copy_text(state->active_url, sizeof(state->active_url), target) != 0 ||
        copy_text(state->address, sizeof(state->address), target) != 0)
        return -28;
    state->address_length = (uint32_t)bounded_length(
        state->address, sizeof(state->address));
    state->active = candidate;
    state->loaded = 1U;
    state->scroll_y = 0U;
    return 0;
}

static int navigate(browser_state_t *state,
                    reist_gui_surface_client_t *client,
                    const char *target) {
    char normalized[BROWSER_URL_CAPACITY];
    int normalize_status;
    if (target != 0 && target[0U] == '#' && state->loaded)
        normalize_status = reist_html_url_resolve(
            state->active_url, target, normalized, sizeof(normalized));
    else
        normalize_status = reist_html_navigation_normalize(
            target, normalized, sizeof(normalized));
    if (normalize_status != 0) {
        set_status(state, "Adresse nicht unterstuetzt");
        return normalize_status;
    }
    set_status(state, "Lade Dokument ...");
    int status = load_candidate(state, client, normalized);
    if (status != 0) {
        set_status(state, "Laden abgelehnt - bisherige Seite bleibt");
        return status;
    }
    const char *title = documents[state->active].title[0U] != '\0'
        ? documents[state->active].title : "REIST Web";
    (void)reist_gui_surface_client_set_title(client, title);
    set_status(state, "Bereit - HTML-Teilsatz, Skripte inert");
    x86os_puts("BROWSER_RENDER_OK\n");
    return 0;
}

static uint32_t viewport_height(const reist_gui_surface_client_t *client) {
    return client->height > BROWSER_CONTENT_TOP + BROWSER_STATUS_HEIGHT
        ? client->height - BROWSER_CONTENT_TOP - BROWSER_STATUS_HEIGHT : 1U;
}

static uint32_t maximum_scroll(const browser_state_t *state,
                               const reist_gui_surface_client_t *client) {
    if (!state->loaded) return 0U;
    uint32_t view = viewport_height(client);
    uint32_t total = layouts[state->active].total_height;
    return total > view ? total - view : 0U;
}

static void set_scroll(browser_state_t *state,
                       const reist_gui_surface_client_t *client,
                       int64_t desired) {
    uint32_t maximum = maximum_scroll(state, client);
    if (desired < 0) desired = 0;
    if ((uint64_t)desired > maximum) desired = maximum;
    if (state->scroll_y != (uint32_t)desired) {
        state->scroll_y = (uint32_t)desired;
        state->redraw = 1U;
    }
}

static int paint_text(reist_gui_surface_client_t *client,
                      int32_t x, int32_t y, uint32_t width,
                      const char *text, uint32_t length,
                      uint32_t foreground, uint32_t background,
                      uint32_t height) {
    if (length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY) return -28;
    return reist_gui_surface_client_paint_font_text(
        client, x, y, width, text, length, foreground, background,
        REIST_GUI_FONT_FAMILY_UNIFONT, height);
}

static int render(browser_state_t *state,
                  reist_gui_surface_client_t *client) {
    static const uint32_t chrome = 0x00D4D0C8U;
    static const uint32_t white = 0x00FFFFFFU;
    static const uint32_t dark = 0x00202020U;
    static const uint32_t link = 0x000000CCU;
    static const uint32_t heading = 0x00203070U;
    static const uint32_t muted = 0x00606060U;
    if (reist_gui_surface_client_paint_begin(client) != 0) return -1;
    reist_gui_rect_t full = {0, 0, client->width, client->height};
    reist_gui_rect_t bar = {10, 10,
        client->width > 20U ? client->width - 20U : 1U, 32U};
    if (reist_gui_surface_client_paint_fill(client, full, white) != 0 ||
        reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){
            0, 0, client->width, BROWSER_CONTENT_TOP}, chrome) != 0 ||
        reist_gui_surface_client_paint_fill(client, bar,
            state->address_focused ? 0x00FFFDE0U : white) != 0)
        return -1;
    uint32_t shown = state->address_length;
    if (shown >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY)
        shown = REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U;
    const char *address = state->address + (state->address_length - shown);
    if (paint_text(client, 16, 18, bar.width > 12U ? bar.width - 12U : 1U,
                   address, shown, dark,
                   state->address_focused ? 0x00FFFDE0U : white,
                   BROWSER_BODY_FONT) != 0) return -1;
    static const char help[] = "Enter: oeffnen  Pfeile/Bild: scrollen";
    if (paint_text(client, 12, 51, client->width > 24U ? client->width - 24U : 1U,
                   help, (uint32_t)(sizeof(help) - 1U),
                   muted, chrome, 14U) != 0) return -1;

    state->hit_count = 0U;
    uint32_t view = viewport_height(client);
    uint32_t body_bottom = BROWSER_CONTENT_TOP + view;
    uint32_t commands = 0U;
    if (state->loaded) {
        const browser_layout_t *layout = &layouts[state->active];
        const reist_html_document_t *document = &documents[state->active];
        for (uint32_t index = 0U; index < layout->run_count; ++index) {
            const browser_layout_run_t *run = &layout->runs[index];
            int64_t screen_y = (int64_t)BROWSER_CONTENT_TOP + run->y -
                               state->scroll_y;
            if (screen_y + run->height <= BROWSER_CONTENT_TOP ||
                screen_y >= body_bottom) continue;
            uint32_t needed = (run->style & REIST_HTML_STYLE_LINK) ? 2U : 1U;
            if (commands + needed > BROWSER_VISIBLE_RUN_BUDGET) break;
            commands += needed;
            if (run->kind == REIST_HTML_ELEMENT_LIST_MARKER) {
                char marker[12U];
                uint32_t marker_length = 0U;
                if (run->text_length == 0U) {
                    marker[marker_length++] = '*';
                } else {
                    char digits[10U]; uint32_t count = 0U;
                    uint32_t value = run->text_length;
                    do {
                        digits[count++] = (char)('0' + value % 10U);
                        value /= 10U;
                    } while (value != 0U && count < sizeof(digits));
                    while (count != 0U)
                        marker[marker_length++] = digits[--count];
                    marker[marker_length++] = '.';
                }
                if (paint_text(client, run->x, (int32_t)screen_y, run->width,
                               marker, marker_length, dark, white,
                               BROWSER_BODY_FONT) != 0) return -1;
                continue;
            }
            uint32_t foreground = run->style & REIST_HTML_STYLE_LINK ? link
                : run->style & (REIST_HTML_STYLE_HEADING_1 |
                                REIST_HTML_STYLE_HEADING_2 |
                                REIST_HTML_STYLE_HEADING_3) ? heading
                : run->style & REIST_HTML_STYLE_ITALIC ? 0x00405050U : dark;
            if (paint_text(client, run->x, (int32_t)screen_y, run->width,
                           document->text + run->text_offset,
                           run->text_length, foreground, white,
                           run->height) != 0) return -1;
            if ((run->style & REIST_HTML_STYLE_LINK) &&
                run->link_index < document->link_count) {
                if (reist_gui_surface_client_paint_fill(client,
                    (reist_gui_rect_t){run->x,
                        (int32_t)screen_y + (int32_t)run->height - 2,
                        run->width, 1U}, link) != 0) return -1;
                if (state->hit_count < BROWSER_LINK_HIT_CAPACITY)
                    state->hits[state->hit_count++] = (browser_link_hit_t){
                        {run->x, (int32_t)screen_y, run->width, run->height},
                        run->link_index};
            }
        }
        uint32_t maximum = maximum_scroll(state, client);
        if (maximum != 0U && client->width >= 12U) {
            uint32_t total = layout->total_height;
            uint32_t thumb = view * view / total;
            if (thumb < 18U) thumb = 18U;
            if (thumb > view) thumb = view;
            uint32_t travel = view - thumb;
            uint32_t top = maximum == 0U ? 0U
                : (state->scroll_y * travel) / maximum;
            if (reist_gui_surface_client_paint_fill(client,
                    (reist_gui_rect_t){(int32_t)client->width - 9,
                    (int32_t)BROWSER_CONTENT_TOP + (int32_t)top,
                    7U, thumb}, 0x00909090U) != 0) return -1;
        }
    }
    uint32_t status_top = client->height > BROWSER_STATUS_HEIGHT
        ? client->height - BROWSER_STATUS_HEIGHT : 0U;
    if (reist_gui_surface_client_paint_fill(client, (reist_gui_rect_t){
            0, (int32_t)status_top,
            client->width, BROWSER_STATUS_HEIGHT}, chrome) != 0)
        return -1;
    uint32_t status_length = (uint32_t)bounded_length(
        state->status, sizeof(state->status));
    if (status_length >= REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY)
        status_length = REIST_GUI_SURFACE_PAINT_TEXT_CAPACITY - 1U;
    if (paint_text(client, 8, (int32_t)status_top + 3,
                   client->width > 16U ? client->width - 16U : 1U,
                   state->status, status_length, dark, chrome, 14U) != 0)
        return -1;
    return reist_gui_surface_client_paint_commit(client);
}

static uint32_t point_in_rect(reist_gui_rect_t rect, int32_t x, int32_t y) {
    return x >= rect.x && y >= rect.y &&
        (uint32_t)(x - rect.x) < rect.width &&
        (uint32_t)(y - rect.y) < rect.height;
}

static void activate_link(browser_state_t *state,
                          reist_gui_surface_client_t *client,
                          uint32_t link_index) {
    if (!state->loaded || link_index >= documents[state->active].link_count)
        return;
    char resolved[BROWSER_URL_CAPACITY];
    if (reist_html_url_resolve(
            state->active_url, documents[state->active].links[link_index].href,
            resolved, sizeof(resolved)) != 0) {
        set_status(state, "Linkziel nicht unterstuetzt");
        return;
    }
    if (navigate(state, client, resolved) == 0)
        x86os_puts("BROWSER_LINK_OK\n");
}

static void handle_pointer(browser_state_t *state,
                           reist_gui_surface_client_t *client,
                           const reist_gui_surface_input_t *input) {
    if (input->type != REIST_GUI_SURFACE_INPUT_POINTER_BUTTON ||
        input->button != 1U || !input->pressed) return;
    if (input->y >= 10 && input->y < 42) {
        state->address_focused = 1U;
        state->address_replace_pending = 1U;
        state->redraw = 1U;
        return;
    }
    state->address_focused = 0U;
    for (uint32_t index = 0U; index < state->hit_count; ++index) {
        if (point_in_rect(state->hits[index].rect, input->x, input->y)) {
            uint32_t link_index = state->hits[index].link_index;
            activate_link(state, client, link_index);
            return;
        }
    }
    state->redraw = 1U;
}

static void handle_keyboard(browser_state_t *state,
                            reist_gui_surface_client_t *client,
                            uint32_t key) {
    if (key == BROWSER_KEY_ESCAPE || key == 27U) {
        state->exit_requested = 1U;
        return;
    }
    if (state->address_focused) {
        if (key == 8U || key == 127U) {
            if (state->address_replace_pending) {
                state->address[0U] = '\0';
                state->address_length = 0U;
                state->address_replace_pending = 0U;
            } else if (state->address_length != 0U) {
                state->address[--state->address_length] = '\0';
            }
            state->redraw = 1U;
        } else if (key == '\r' || key == '\n') {
            char target[BROWSER_URL_CAPACITY];
            if (copy_text(target, sizeof(target), state->address) == 0)
                (void)navigate(state, client, target);
            state->address_focused = 0U;
        } else if (key >= 0x20U && key <= 0x7EU) {
            if (state->address_replace_pending) {
                state->address_length = 0U;
                state->address[0U] = '\0';
                state->address_replace_pending = 0U;
            }
            if (state->address_length + 1U < sizeof(state->address)) {
                state->address[state->address_length++] = (char)key;
                state->address[state->address_length] = '\0';
                state->redraw = 1U;
            }
        }
        return;
    }
    uint32_t page = viewport_height(client);
    if (key == BROWSER_KEY_UP)
        set_scroll(state, client, (int64_t)state->scroll_y - 24);
    else if (key == BROWSER_KEY_DOWN)
        set_scroll(state, client, (int64_t)state->scroll_y + 24);
    else if (key == BROWSER_KEY_PAGE_UP)
        set_scroll(state, client, (int64_t)state->scroll_y - page);
    else if (key == BROWSER_KEY_PAGE_DOWN || key == ' ')
        set_scroll(state, client, (int64_t)state->scroll_y + page);
    else if (key == BROWSER_KEY_HOME)
        set_scroll(state, client, 0);
    else if (key == BROWSER_KEY_END)
        set_scroll(state, client, maximum_scroll(state, client));
    else if (key == 'r' || key == 'R')
        (void)navigate(state, client, state->active_url);
    else if (key == '\r' || key == '\n') {
        state->address_focused = 1U;
        state->address_replace_pending = 1U;
        state->redraw = 1U;
    }
}

static int probe_address_input(browser_state_t *state,
                               reist_gui_surface_client_t *client) {
    static const char local[] = "/htdocs/index.html";
    reist_gui_surface_input_t focus = {0};
    focus.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON;
    focus.button = 1U;
    focus.pressed = 1U;
    focus.x = 20;
    focus.y = 20;
    handle_pointer(state, client, &focus);
    for (uint32_t index = 0U; local[index] != '\0'; ++index)
        handle_keyboard(state, client, (uint32_t)(uint8_t)local[index]);
    handle_keyboard(state, client, '\n');
    if (!text_equal(state->address, local) ||
        !text_equal(state->active_url, local)) return -1;
    x86os_puts("BROWSER_ADDRESS_REPLACE_OK\n");

    char normalized[BROWSER_URL_CAPACITY];
    if (reist_html_navigation_normalize(
            "example.test/docs", normalized, sizeof(normalized)) != 0 ||
        !text_equal(normalized, "https://example.test/docs")) return -1;
    x86os_puts("BROWSER_HTTPS_DEFAULT_OK\n");
    return 0;
}

static const char *initial_target(int argc, char **argv, uint32_t *probe) {
    const char *target = "/htdocs/index.html";
    for (int index = 1; index < argc; ++index) {
        if (text_equal(argv[index], "--browser-probe")) *probe = 1U;
        else if (!text_prefix(argv[index], "--reist-surface="))
            target = argv[index];
    }
    return target;
}

int main(int argc, char **argv) {
    x86os_ipc_handle_t endpoint = 0U;
    if (reist_gui_surface_endpoint_from_argv(argc, argv, &endpoint) != 0) {
        x86os_puts("browser: compositor endpoint missing\n");
        return 2;
    }
    reist_gui_surface_client_t client;
    if (reist_gui_surface_client_init(&client, endpoint) != 0) return 1;
    int status = -9;
    for (uint32_t attempt = 0U; attempt < BROWSER_CREATE_ATTEMPTS; ++attempt) {
        status = reist_gui_surface_client_create(
            &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL, 800U, 600U);
        if (status == 0 || (status != -9 && status != -13)) break;
        (void)x86os_sleep_ms(1U);
    }
    if (status != 0 || reist_gui_surface_client_ack_configure(
            &client, client.configured_serial) != 0 ||
        reist_gui_surface_client_set_title(&client, "REIST Web") != 0) {
        (void)x86os_ipc_release(endpoint);
        return 1;
    }

    static browser_state_t state;
    make_temporary_path(&state);
    const char *target = initial_target(argc, argv, &state.probe);
    if (copy_text(state.address, sizeof(state.address), target) != 0)
        copy_text(state.address, sizeof(state.address), "/htdocs/index.html");
    state.address_length = (uint32_t)bounded_length(
        state.address, sizeof(state.address));
    state.address_focused = 1U;
    state.address_replace_pending = 1U;
    state.redraw = 1U;
    (void)navigate(&state, &client, state.address);

    while (!state.exit_requested) {
        if (state.redraw) {
            int paint_status = -11;
            for (uint32_t attempt = 0U; attempt < BROWSER_PAINT_ATTEMPTS;
                 ++attempt) {
                paint_status = render(&state, &client);
                if (paint_status == 0) break;
                (void)x86os_sleep_ms(5U);
            }
            if (paint_status != 0) break;
            state.redraw = 0U;
            if (state.probe && state.loaded && state.probe_phase == 0U) {
                if (probe_address_input(&state, &client) != 0)
                    x86os_puts("BROWSER_PROBE_FAIL address-input\n");
                state.probe_phase = 1U;
            } else if (state.probe && state.loaded &&
                       state.probe_phase == 1U) {
                if (state.hit_count == 0U) {
                    x86os_puts("BROWSER_PROBE_FAIL link-hit\n");
                } else {
                    reist_gui_surface_input_t click = {0};
                    click.type = REIST_GUI_SURFACE_INPUT_POINTER_BUTTON;
                    click.button = 1U;
                    click.pressed = 1U;
                    click.x = state.hits[0U].rect.x + 1;
                    click.y = state.hits[0U].rect.y + 1;
                    handle_pointer(&state, &client, &click);
                }
                state.probe_phase = 2U;
            } else if (state.probe && state.loaded &&
                       state.probe_phase == 2U) {
                uint32_t maximum = maximum_scroll(&state, &client);
                set_scroll(&state, &client, maximum);
                x86os_puts(maximum != 0U
                    ? "BROWSER_SCROLL_OK\n" : "BROWSER_PROBE_FAIL scroll\n");
                state.probe_phase = 3U;
            } else if (state.probe && state.loaded &&
                       state.probe_phase == 3U) {
                if (navigate(&state, &client, state.active_url) == 0)
                    x86os_puts("BROWSER_RELOAD_OK\n");
                state.probe_phase = 4U;
            } else if (state.probe && state.loaded &&
                       state.probe_phase == 4U) {
                x86os_puts("BROWSER_RELOAD_PAINTED\n");
                (void)x86os_sleep_ms(1000U);
                state.exit_requested = 1U;
                state.probe_phase = 5U;
            }
        }
        reist_gui_surface_message_t message;
        status = reist_gui_surface_client_receive(&client, &message, 0U);
        if (status == -11) {
            (void)x86os_sleep_ms(5U);
            continue;
        }
        if (status != 0 || message.type == REIST_GUI_SURFACE_CLOSE) break;
        if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
            if (reist_gui_surface_client_accept_configure(
                    &client, &message) != 0) break;
            if (state.loaded && build_layout(&documents[state.active],
                    client.width, &layouts[state.active]) != 0) {
                set_status(&state, "Layoutkapazitaet erschoepft");
            }
            set_scroll(&state, &client, state.scroll_y);
            state.redraw = 1U;
        } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                   message.input.type == REIST_GUI_SURFACE_INPUT_KEYBOARD &&
                   message.input.pressed) {
            handle_keyboard(&state, &client, message.input.key);
        } else if (message.type == REIST_GUI_SURFACE_INPUT) {
            handle_pointer(&state, &client, &message.input);
        }
    }
    x86os_puts("BROWSER_CLOSE_OK\n");
    (void)x86os_unlink(state.temporary_path);
    (void)reist_gui_surface_client_destroy(&client);
    (void)x86os_ipc_release(endpoint);
    return 0;
}
