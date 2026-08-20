/** @file main.c @brief Graphical BMP/GIF viewer using libreistimage only. */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "reist/image.h"

#define VIEWER_FILE_CAPACITY (1024U * 1024U)
#define VIEWER_MOUSE_BATCH_LIMIT 32U

static uint8_t encoded[VIEWER_FILE_CAPACITY];
static uint32_t pixels[REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT];
static reist_image_workspace_t workspace;

static int load_file(const char *path, size_t *size_out) {
    x86os_file_info_t info;
    if (path == 0 || size_out == 0 || x86os_stat(path, &info) != 0 ||
        info.type != X86OS_FILE || info.size == 0U ||
        info.size > sizeof(encoded)) return -2;
    int descriptor = x86os_open(path);
    if (descriptor < 0) return descriptor;
    size_t done = 0U;
    while (done < info.size) {
        int result = x86os_read(descriptor, &encoded[done], info.size - done);
        if (result <= 0) { (void)x86os_close(descriptor); return -5; }
        done += (size_t)result;
    }
    int result = x86os_close(descriptor);
    if (result != 0) return result;
    *size_out = done;
    return 0;
}

static int draw_image(const x86os_display_info_t *display,
                      const reist_image_info_t *image) {
    uint32_t margin = 20U, title = 34U, footer = 30U;
    uint32_t available_width = display->width > margin * 2U
        ? display->width - margin * 2U : 1U;
    uint32_t available_height = display->height > title + footer + margin
        ? display->height - title - footer - margin : 1U;
    uint32_t scale_x = image->width > available_width
        ? (image->width + available_width - 1U) / available_width : 1U;
    uint32_t scale_y = image->height > available_height
        ? (image->height + available_height - 1U) / available_height : 1U;
    uint32_t divisor = scale_x > scale_y ? scale_x : scale_y;
    uint32_t draw_width = (image->width + divisor - 1U) / divisor;
    uint32_t draw_height = (image->height + divisor - 1U) / divisor;
    int32_t origin_x = (int32_t)((display->width - draw_width) / 2U);
    int32_t origin_y = (int32_t)(title +
        (available_height > draw_height ? (available_height - draw_height) / 2U : 0U));
    (void)x86os_fill_rect(0, 0, display->width, display->height, 0x00303030U);
    (void)x86os_fill_rect(0, 0, display->width, title, 0x00000088U);
    const char *caption = image->format == REIST_IMAGE_FORMAT_BMP
        ? "REIST Image Viewer - BMP" : "REIST Image Viewer - GIF";
    size_t caption_length = 0U;
    while (caption[caption_length] != '\0') ++caption_length;
    (void)x86os_draw_text_pixels(12, 9, caption, caption_length,
                                 0x00FFFFFFU, 0x00000088U);
    /* Nearest-neighbour scaling compacts safely in place: every selected
     * source index is at or ahead of its destination index.  The completed
     * rectangle is then transferred and presented with one display request. */
    for (uint32_t y = 0U; y < draw_height; ++y) {
        uint32_t source_y = y * divisor;
        for (uint32_t x = 0U; x < draw_width; ++x)
            pixels[(size_t)y * draw_width + x] =
                pixels[(size_t)source_y * image->stride_pixels + x * divisor];
    }
    int result = x86os_draw_pixels(origin_x, origin_y, draw_width, draw_height,
                                   pixels, draw_width);
    const char *help = "ESC oder Mausklick: Schliessen";
    size_t help_length = 0U;
    while (help[help_length] != '\0') ++help_length;
    (void)x86os_draw_text_pixels(12, (int32_t)display->height - 22,
                                 help, help_length, 0x00FFFFFFU, 0x00303030U);
    return result;
}

int main(int argc, char **argv) {
    if (argc != 2) { x86os_puts("Usage: imageviewer <bmp-or-gif>\n"); return 2; }
    size_t encoded_size = 0U;
    int result = load_file(argv[1], &encoded_size);
    reist_image_info_t image;
    if (result == 0) result = reist_image_decode(
        encoded, encoded_size, pixels,
        REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT, &workspace, &image);
    if (result != 0) { x86os_puts("imageviewer: Bild ungueltig\n"); return 1; }
    x86os_display_info_t display;
    uint32_t activated = 0U;
    if (x86os_display_info(&display) != 0) {
        if (x86os_display_activate() != 0 || x86os_display_info(&display) != 0)
            return 1;
        activated = 1U;
    }
    if (draw_image(&display, &image) != 0) {
        if (activated) (void)x86os_display_deactivate();
        return 1;
    }
    int32_t pointer_x = (int32_t)(display.width / 2U);
    int32_t pointer_y = (int32_t)(display.height / 2U);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
    uint32_t done = 0U;
    while (!done) {
        uint32_t count = 0U;
        for (; count < VIEWER_MOUSE_BATCH_LIMIT; ++count) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            pointer_x += mouse.delta_x; pointer_y += mouse.delta_y;
            if (pointer_x < 0) pointer_x = 0;
            if (pointer_y < 0) pointer_y = 0;
            if (pointer_x >= (int32_t)display.width) pointer_x = display.width - 1U;
            if (pointer_y >= (int32_t)display.height) pointer_y = display.height - 1U;
            if ((mouse.buttons & X86OS_MOUSE_BUTTON_LEFT) != 0U) done = 1U;
        }
        (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
        if (x86os_getchar_nonblocking() == 0x1B) done = 1U;
        if (count == 0U) (void)x86os_sleep_ms(5U);
    }
    (void)x86os_pointer_update(pointer_x, pointer_y, 0U);
    if (activated) return x86os_display_deactivate() == 0 ? 0 : 1;
    x86os_clear();
    return 0;
}
