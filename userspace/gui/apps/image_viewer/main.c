/**
 * @file main.c
 * @brief Windowed BMP/GIF viewer using libreistimage and the Surface ABI.
 *
 * Desktop instances publish immutable, generation-scoped pixel buffers.
 * Direct display mode is retained only as a shell-compatible fallback.
 */
#include <stddef.h>
#include <stdint.h>

#include "x86os.h"
#include "reist/image.h"
#include "reist/vfs_file_client.h"
#include "reist/gui/surface.h"
#include "reist/gui/surface_client.h"

#define VIEWER_FILE_CAPACITY (1024U * 1024U)
#define VIEWER_EVENT_BATCH_LIMIT 32U
#define VIEWER_DEFAULT_WIDTH 760U
#define VIEWER_DEFAULT_HEIGHT 540U
#define VIEWER_MARGIN 12U
#define VIEWER_BACKGROUND 0x00303030U

static uint8_t encoded[VIEWER_FILE_CAPACITY];
static uint32_t image_pixels[REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT];
static reist_image_workspace_t workspace;
static uint32_t *surface_pixels;
static uint32_t surface_pixel_capacity;

static int starts_with(const char *text, const char *prefix) {
    if (text == 0 || prefix == 0) return 0;
    while (*prefix != '\0') {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static const char *image_path_from_argv(int argc, char **argv) {
    if (argv == 0) return 0;
    for (int index = 1; index < argc; ++index)
        if (argv[index] != 0 &&
            !starts_with(argv[index], "--reist-surface="))
            return argv[index];
    return 0;
}

static int load_file(const char *path, size_t *size_out) {
    if (path == 0 || size_out == 0) return -22;
    *size_out = 0U;
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    int result = reist_vfs_file_open(
        path, REIST_VFS_FILE_DEFAULT_TIMEOUT_MS, &handle);
    if (result != 0) return result;
    x86os_file_info_t info;
    result = reist_vfs_file_fstat(handle, &info);
    if (result != 0 || info.type != X86OS_FILE || info.size == 0U ||
        info.size > sizeof(encoded)) {
        (void)reist_vfs_file_close(handle);
        return result != 0 ? result : -2;
    }
    size_t done = 0U;
    while (done < info.size) {
        size_t amount = info.size - done;
        if (amount > X86OS_STORAGE_BULK_MAX_BYTES)
            amount = X86OS_STORAGE_BULK_MAX_BYTES;
        result = reist_vfs_file_read_bulk(handle, &encoded[done], amount);
        if (result <= 0 || (size_t)result > amount) {
            (void)reist_vfs_file_close(handle);
            return -5;
        }
        done += (size_t)result;
    }
    result = reist_vfs_file_close(handle);
    if (result != 0) return result;
    *size_out = done;
    x86os_puts("IMAGEVIEWER_BULK_LOAD_OK\n");
    return 0;
}

static void fit_image(const reist_image_info_t *image,
                      uint32_t available_width, uint32_t available_height,
                      uint32_t *draw_width, uint32_t *draw_height) {
    *draw_width = image->width;
    *draw_height = image->height;
    if (*draw_width <= available_width && *draw_height <= available_height)
        return;
    if (image->width * available_height >
        image->height * available_width) {
        *draw_width = available_width;
        *draw_height = image->height * available_width / image->width;
    } else {
        *draw_height = available_height;
        *draw_width = image->width * available_height / image->height;
    }
    if (*draw_width == 0U) *draw_width = 1U;
    if (*draw_height == 0U) *draw_height = 1U;
}

static int raster_surface(uint32_t width, uint32_t height,
                          const reist_image_info_t *image) {
    if (width == 0U || height == 0U ||
        width > REIST_GUI_SURFACE_MAX_WIDTH ||
        height > REIST_GUI_SURFACE_MAX_HEIGHT ||
        height > UINT32_MAX / width) return -22;
    uint32_t required = width * height;
    if (required > surface_pixel_capacity) {
        if (required > UINT32_MAX / sizeof(uint32_t)) return -22;
        uint32_t *replacement = (uint32_t *)x86os_realloc(
            surface_pixels, (size_t)required * sizeof(uint32_t));
        if (replacement == 0) return -12;
        surface_pixels = replacement;
        surface_pixel_capacity = required;
    }
    for (uint32_t index = 0U; index < required; ++index)
        surface_pixels[index] = VIEWER_BACKGROUND;

    uint32_t available_width = width > VIEWER_MARGIN * 2U
        ? width - VIEWER_MARGIN * 2U : 1U;
    uint32_t available_height = height > VIEWER_MARGIN * 2U
        ? height - VIEWER_MARGIN * 2U : 1U;
    uint32_t draw_width, draw_height;
    fit_image(image, available_width, available_height,
              &draw_width, &draw_height);
    uint32_t origin_x = (width - draw_width) / 2U;
    uint32_t origin_y = (height - draw_height) / 2U;
    for (uint32_t y = 0U; y < draw_height; ++y) {
        uint32_t source_y = y * image->height / draw_height;
        for (uint32_t x = 0U; x < draw_width; ++x) {
            uint32_t source_x = x * image->width / draw_width;
            surface_pixels[(origin_y + y) * width + origin_x + x] =
                image_pixels[source_y * image->stride_pixels + source_x] &
                0x00FFFFFFU;
        }
    }
    return 0;
}

static int discard_released_buffer(
    reist_gui_surface_client_t *client, uint32_t id, uint32_t generation) {
    int result = reist_gui_surface_client_buffer_destroy(
        client, id, generation);
    if (result == 0)
        result = x86os_display_surface_buffer_destroy(id, generation);
    return result;
}

static int publish_surface_frame(
    reist_gui_surface_client_t *client, const reist_image_info_t *image,
    uint32_t *active_id, uint32_t *active_generation) {
    int result = raster_surface(client->width, client->height, image);
    if (result != 0) return result;

    uint32_t new_id = 0U, new_generation = 0U;
    result = x86os_display_surface_buffer_create(
        client->width, client->height, surface_pixels, client->width,
        &new_id, &new_generation);
    if (result != 0) return result;
    reist_gui_surface_buffer_t descriptor = {
        REIST_GUI_SURFACE_BUFFER_API_VERSION, sizeof(descriptor),
        new_id, new_generation, client->width, client->height,
        client->width * sizeof(uint32_t),
        REIST_GUI_SURFACE_BUFFER_FORMAT_XRGB8888,
        client->width * client->height * sizeof(uint32_t), 0U,
    };
    uint32_t registered = 0U;
    result = reist_gui_surface_client_buffer_create(client, &descriptor);
    if (result == 0) registered = 1U;
    if (result == 0)
        result = reist_gui_surface_client_attach(
            client, new_id, new_generation);
    if (result == 0)
        result = reist_gui_surface_client_damage(
            client, (reist_gui_rect_t){
                0, 0, client->width, client->height});
    uint32_t released_id = 0U, released_generation = 0U;
    if (result == 0)
        result = reist_gui_surface_client_commit_with_release(
            client, &released_id, &released_generation);
    if (result != 0) {
        if (registered)
            (void)reist_gui_surface_client_buffer_destroy(
                client, new_id, new_generation);
        (void)x86os_display_surface_buffer_destroy(new_id, new_generation);
        return result;
    }
    if (released_id != 0U) {
        if (released_id != *active_id ||
            released_generation != *active_generation) return -84;
        result = discard_released_buffer(
            client, released_id, released_generation);
        if (result != 0) return result;
    }
    *active_id = new_id;
    *active_generation = new_generation;
    return 0;
}

static int run_surface_viewer(x86os_ipc_handle_t endpoint,
                              const reist_image_info_t *image) {
    reist_gui_surface_client_t client;
    int result = reist_gui_surface_client_init(&client, endpoint);
    if (result == 0) {
        result = -9;
        for (uint32_t attempt = 0U; attempt < 250U; ++attempt) {
            result = reist_gui_surface_client_create(
                &client, REIST_GUI_SURFACE_ROLE_TOPLEVEL,
                VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT);
            if (result == 0 || (result != -9 && result != -13)) break;
            (void)x86os_sleep_ms(1U);
        }
    }
    if (result == 0)
        result = reist_gui_surface_client_ack_configure(
            &client, client.configured_serial);
    if (result == 0)
        result = reist_gui_surface_client_set_title(
            &client, image->format == REIST_IMAGE_FORMAT_BMP
                ? "REIST Image Viewer - BMP"
                : "REIST Image Viewer - GIF");
    uint32_t active_id = 0U, active_generation = 0U;
    if (result == 0)
        result = publish_surface_frame(
            &client, image, &active_id, &active_generation);
    if (result != 0) {
        (void)x86os_ipc_release(endpoint);
        return result;
    }
    x86os_puts("IMAGEVIEWER_SURFACE_READY\n");

    uint32_t done = 0U;
    while (!done) {
        uint32_t processed = 0U;
        for (; processed < VIEWER_EVENT_BATCH_LIMIT; ++processed) {
            reist_gui_surface_message_t message;
            int receive = reist_gui_surface_client_receive(
                &client, &message, 0U);
            if (receive == -11) break;
            if (receive != 0) {
                result = receive;
                done = 1U;
                break;
            }
            if (message.type == REIST_GUI_SURFACE_CLOSE) {
                done = 1U;
            } else if (message.type == REIST_GUI_SURFACE_CONFIGURE) {
                result = reist_gui_surface_client_accept_configure(
                    &client, &message);
                if (result == 0)
                    result = publish_surface_frame(
                        &client, image, &active_id, &active_generation);
                if (result != 0) done = 1U;
            } else if (message.type == REIST_GUI_SURFACE_INPUT &&
                       message.input.type ==
                           REIST_GUI_SURFACE_INPUT_KEYBOARD &&
                       message.input.pressed &&
                       message.input.key == 0x1BU) {
                done = 1U;
            }
        }
        if (!done && processed == 0U) (void)x86os_sleep_ms(5U);
    }
    (void)reist_gui_surface_client_destroy(&client);
    (void)x86os_ipc_release(endpoint);
    if (surface_pixels != 0) {
        x86os_free(surface_pixels);
        surface_pixels = 0;
        surface_pixel_capacity = 0U;
    }
    return result;
}

static int draw_fullscreen(const x86os_display_info_t *display,
                           const reist_image_info_t *image) {
    uint32_t margin = 20U, title = 34U, footer = 30U;
    uint32_t available_width = display->width > margin * 2U
        ? display->width - margin * 2U : 1U;
    uint32_t available_height = display->height > title + footer + margin
        ? display->height - title - footer - margin : 1U;
    uint32_t draw_width, draw_height;
    fit_image(image, available_width, available_height,
              &draw_width, &draw_height);
    int32_t origin_x = (int32_t)((display->width - draw_width) / 2U);
    int32_t origin_y = (int32_t)(title +
        (available_height > draw_height
            ? (available_height - draw_height) / 2U : 0U));
    (void)x86os_fill_rect(
        0, 0, display->width, display->height, VIEWER_BACKGROUND);
    (void)x86os_fill_rect(0, 0, display->width, title, 0x00000088U);
    const char *caption = image->format == REIST_IMAGE_FORMAT_BMP
        ? "REIST Image Viewer - BMP" : "REIST Image Viewer - GIF";
    size_t caption_length = 0U;
    while (caption[caption_length] != '\0') ++caption_length;
    (void)x86os_draw_text_pixels(
        12, 9, caption, caption_length, 0x00FFFFFFU, 0x00000088U);
    uint32_t *scaled = (uint32_t *)x86os_malloc(
        (size_t)draw_width * draw_height * sizeof(uint32_t));
    if (scaled == 0) return -12;
    for (uint32_t y = 0U; y < draw_height; ++y) {
        uint32_t source_y = y * image->height / draw_height;
        for (uint32_t x = 0U; x < draw_width; ++x) {
            uint32_t source_x = x * image->width / draw_width;
            scaled[y * draw_width + x] = image_pixels[
                source_y * image->stride_pixels + source_x];
        }
    }
    int result = x86os_draw_pixels(
        origin_x, origin_y, draw_width, draw_height, scaled, draw_width);
    x86os_free(scaled);
    const char *help = "ESC oder Mausklick: Schliessen";
    size_t help_length = 0U;
    while (help[help_length] != '\0') ++help_length;
    (void)x86os_draw_text_pixels(
        12, (int32_t)display->height - 22, help, help_length,
        0x00FFFFFFU, VIEWER_BACKGROUND);
    return result;
}

static int run_fullscreen_viewer(const reist_image_info_t *image) {
    x86os_display_info_t display;
    uint32_t activated = 0U;
    if (x86os_display_info(&display) != 0) {
        if (x86os_display_activate() != 0 ||
            x86os_display_info(&display) != 0) return 1;
        activated = 1U;
    }
    if (draw_fullscreen(&display, image) != 0) {
        if (activated) (void)x86os_display_deactivate();
        return 1;
    }
    int32_t pointer_x = (int32_t)(display.width / 2U);
    int32_t pointer_y = (int32_t)(display.height / 2U);
    (void)x86os_pointer_update(pointer_x, pointer_y, 1U);
    uint32_t done = 0U;
    while (!done) {
        uint32_t count = 0U;
        for (; count < VIEWER_EVENT_BATCH_LIMIT; ++count) {
            x86os_mouse_event_t mouse;
            if (x86os_mouse_event(&mouse) != 0) break;
            pointer_x += mouse.delta_x;
            pointer_y += mouse.delta_y;
            if (pointer_x < 0) pointer_x = 0;
            if (pointer_y < 0) pointer_y = 0;
            if (pointer_x >= (int32_t)display.width)
                pointer_x = (int32_t)display.width - 1;
            if (pointer_y >= (int32_t)display.height)
                pointer_y = (int32_t)display.height - 1;
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

int main(int argc, char **argv) {
    const char *path = image_path_from_argv(argc, argv);
    if (path == 0) {
        x86os_puts("Usage: imageviewer <bmp-or-gif>\n");
        return 2;
    }
    size_t encoded_size = 0U;
    int result = load_file(path, &encoded_size);
    reist_image_info_t image;
    if (result == 0)
        result = reist_image_decode(
            encoded, encoded_size, image_pixels,
            REIST_IMAGE_MAX_WIDTH * REIST_IMAGE_MAX_HEIGHT,
            &workspace, &image);
    if (result != 0) {
        x86os_puts("imageviewer: Bild ungueltig\n");
        return 1;
    }
    x86os_ipc_handle_t endpoint = 0U;
    int endpoint_status = reist_gui_surface_endpoint_from_argv(
        argc, argv, &endpoint);
    if (endpoint_status == 0)
        return run_surface_viewer(endpoint, &image) == 0 ? 0 : 1;
    if (endpoint_status != -2) {
        x86os_puts("imageviewer: Surface-Endpunkt ungueltig\n");
        return 1;
    }
    return run_fullscreen_viewer(&image);
}
