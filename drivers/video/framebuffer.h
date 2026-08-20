/**
 * @file drivers/video/framebuffer.h
 * @brief Linearer Framebuffervertrag.
 *
 * Layer: Ring-0 display driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Pitch, Pixelformat, Abmessungen und physischer Bereich müssen validiert sein.
 */
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Framebuffer configuration
#define FB_WIDTH 1024
#define FB_HEIGHT 768
#define FB_BPP 32  // Bits per pixel (32-bit color)

// Multiboot framebuffer info structure
typedef struct {
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
} multiboot_framebuffer_info_t;

// Font dimensions
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

/* Stable Ring-3 drawing contract.  Colors passed to these APIs use packed
 * 0x00RRGGBB regardless of the framebuffer's native channel layout. */
#define FRAMEBUFFER_DISPLAY_ABI_VERSION 1U
#define FRAMEBUFFER_DISPLAY_MAX_TEXT 256U

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bits_per_pixel;
    uint32_t red_field_position;
    uint32_t red_mask_size;
    uint32_t green_field_position;
    uint32_t green_mask_size;
    uint32_t blue_field_position;
    uint32_t blue_mask_size;
    uint32_t font_width;
    uint32_t font_height;
} framebuffer_display_info_t;

// Color definitions (32-bit ARGB)
#define FB_COLOR_BLACK      0xFF000000
#define FB_COLOR_BLUE       0xFF0000AA
#define FB_COLOR_GREEN      0xFF00AA00
#define FB_COLOR_CYAN       0xFF00AAAA
#define FB_COLOR_RED        0XFFAA0000
#define FB_COLOR_MAGENTA    0xFFAA00AA
#define FB_COLOR_BROWN      0xFFAA5500
#define FB_COLOR_LIGHT_GRAY 0xFFAAAAAA
#define FB_COLOR_DARK_GRAY  0xFF555555
#define FB_COLOR_LIGHT_BLUE 0xFF5555FF
#define FB_COLOR_LIGHT_GREEN 0xFF55FF55
#define FB_COLOR_LIGHT_CYAN 0xFF55FFFF
#define FB_COLOR_LIGHT_RED  0xFFFF5555
#define FB_COLOR_LIGHT_MAGENTA 0xFFFF55FF
#define FB_COLOR_YELLOW     0xFFFFFF55
#define FB_COLOR_WHITE      0xFFFFFFFF

// Function prototypes
void framebuffer_init(multiboot_framebuffer_info_t* fb_info);
void framebuffer_init_runtime(multiboot_framebuffer_info_t* fb_info);
void framebuffer_shutdown(void);
void framebuffer_clear();
void framebuffer_putchar(char c);
void framebuffer_write_string(const char* str);
void framebuffer_set_color(uint32_t fg, uint32_t bg);
void framebuffer_scroll();
void framebuffer_get_cursor(int* x, int* y);
void framebuffer_set_cursor(int x, int y);

// Check if framebuffer is available
bool framebuffer_available();
bool framebuffer_get_display_info(framebuffer_display_info_t* info);
bool framebuffer_fill_rect(int32_t x, int32_t y, uint32_t width,
                           uint32_t height, uint32_t rgb);
/* Internal trusted-buffer helpers used by the bounded pixel-upload syscall.
 * Span writes modify the shadow framebuffer; presentation is explicit so a
 * complete image becomes visible in one damage update. */
bool framebuffer_write_xrgb8888_span(uint32_t x, uint32_t y,
                                     const uint32_t *pixels, uint32_t count);
bool framebuffer_present_pixels(uint32_t x, uint32_t y,
                                uint32_t width, uint32_t height);
int framebuffer_surface_buffer_create(
    int owner_pid, uint32_t owner_generation,
    int consumer_pid, uint32_t consumer_generation,
    uint32_t width, uint32_t height, uint32_t stride_pixels,
    const uint32_t *pixels, uint32_t pixel_count,
    uint32_t *buffer_id, uint32_t *buffer_generation);
int framebuffer_surface_buffer_destroy(
    int owner_pid, uint32_t owner_generation,
    uint32_t buffer_id, uint32_t buffer_generation);
int framebuffer_surface_buffer_draw(
    int consumer_pid, uint32_t consumer_generation,
    int owner_pid, uint32_t owner_generation,
    uint32_t buffer_id, uint32_t buffer_generation,
    uint32_t source_x, uint32_t source_y,
    int32_t destination_x, int32_t destination_y,
    uint32_t width, uint32_t height, uint64_t now_ms);
bool framebuffer_draw_text_pixels(int32_t x, int32_t y, const char* text,
                                  size_t length, uint32_t foreground_rgb,
                                  uint32_t background_rgb);
bool framebuffer_draw_text_pixels_clipped(
    int32_t x, int32_t y, const char *text, size_t length,
    uint32_t foreground_rgb, uint32_t background_rgb,
    int32_t clip_x, int32_t clip_y, uint32_t clip_width,
    uint32_t clip_height);
bool framebuffer_cursor_update(int32_t x, int32_t y, bool visible);
int framebuffer_frame_begin(int owner_pid, uint32_t owner_generation,
                            uint64_t now_ms, uint32_t *serial_out);
int framebuffer_frame_commit(int owner_pid, uint32_t owner_generation,
                             uint32_t serial, uint64_t now_ms);
int framebuffer_frame_cancel(int owner_pid, uint32_t owner_generation,
                             uint32_t serial);
int framebuffer_frame_stage_blit(int owner_pid, uint32_t owner_generation,
                                 uint32_t serial, uint64_t now_ms,
                                 uint32_t source_x, uint32_t source_y,
                                 uint32_t destination_x,
                                 uint32_t destination_y,
                                 uint32_t width, uint32_t height);
int framebuffer_frame_draw_enter(int owner_pid, uint32_t owner_generation,
                                 uint64_t now_ms);
int framebuffer_frame_draw_leave(int owner_pid, uint32_t owner_generation,
                                 uint64_t now_ms);
void framebuffer_frame_process_cleanup(int owner_pid,
                                       uint32_t owner_generation);

#endif // FRAMEBUFFER_H
