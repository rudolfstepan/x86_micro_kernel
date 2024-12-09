#include "framebuffer.h"
#include "kernel/multiboot.h"
#include "vga_font.h"
#include "toolchain/stdio.h"


uint32_t cursor_x = 0;
uint32_t cursor_y = 0;
uint32_t line_height = 20; // Height of a single text line (e.g., 8x16 font)

static uint32_t txt_color = WHITE;
static uint32_t bg_color = BLACK;
static framebuffer_info_t fb_info;

void set_color(uint32_t color) {
    txt_color = color;
}

void parse_framebuffer(multiboot2_tag_framebuffer_t *fb) {
    if (fb == NULL) {
        printf("Framebuffer tag not found.\n");
        return;
    }

    fb_info.address = fb->framebuffer_addr;
    fb_info.pitch = fb->framebuffer_pitch;
    fb_info.width = fb->framebuffer_width;
    fb_info.height = fb->framebuffer_height;
    fb_info.bpp = fb->framebuffer_bpp;

    printf("Framebuffer Info:\n");
    printf("  Address: 0x%lx\n", fb_info.address);
    printf("  Resolution: %ux%u\n", fb_info.width, fb_info.height);
    printf("  Bits Per Pixel: %u\n", fb_info.bpp);
    printf("  Pitch: %u bytes per scanline\n", fb_info.pitch);
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t font_color) {
    uint32_t *framebuffer = (uint32_t *)fb_info.address;

    for (uint8_t row = 0; row < 16; row++) {
        uint8_t row_data = font[(uint8_t)c + 2][row];
        for (uint8_t col = 0; col < 8; col++) {
            if (row_data & (1 << (7 - col))) {
                framebuffer[((y + row) * fb_info.pitch / 4) + (x + col)] = font_color;
            }
        }
    }
}

void draw_string(const char *str, uint32_t font_color, uint32_t bg_color) {
    while (*str) {
        put_char(*str++, font_color, bg_color);
    }
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t font_color) {
    if (x >= fb_info.width || y >= fb_info.height) {
        return; // Out of bounds
    }

    uint32_t *framebuffer = (uint32_t *)fb_info.address;
    framebuffer[(y * fb_info.pitch / 4) + x] = font_color;
}

void render_gradient() {
    for (uint32_t y = 0; y < fb_info.height; y++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            uint32_t color = ((x * 255 / fb_info.width) << 16) | // Red
                             ((y * 255 / fb_info.height) << 8); // Green
            draw_pixel(x, y, color);
        }
    }
}

void clear_screen() {
    uint32_t *framebuffer = (uint32_t *)fb_info.address;
    for (uint32_t y = 0; y < fb_info.height; y++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            framebuffer[(y * fb_info.pitch / 4) + x] = bg_color;
        }
    }
    // Reset cursor position
    cursor_x = 0;
    cursor_y = 0;
}

void fill_screen(uint32_t bg_color) {
    for (uint32_t y = 0; y < fb_info.height; y++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            draw_pixel(x, y, bg_color);
        }
    }
}

void clear_line(uint32_t y, uint32_t color) {
    uint32_t *framebuffer = (uint32_t *)fb_info.address;
    for (uint32_t row = 0; row < line_height; row++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            framebuffer[((y + row) * fb_info.pitch / 4) + x] = color;
        }
    }
}

void scroll_screen(uint32_t bg_color) {
    uint32_t *framebuffer = (uint32_t *)fb_info.address;
    uint32_t screen_width = fb_info.width;
    uint32_t screen_height = fb_info.height;
    uint32_t pitch_in_pixels = fb_info.pitch / 4; // Convert pitch from bytes to pixels

    // Calculate the number of pixels in one line
    uint32_t line_size = line_height * pitch_in_pixels;

    // Copy lines from bottom to top to avoid overlap
    for (uint32_t y = line_height; y < screen_height; y++) {
        for (uint32_t x = 0; x < screen_width; x++) {
            framebuffer[((y - line_height) * pitch_in_pixels) + x] =
                framebuffer[(y * pitch_in_pixels) + x];
        }
    }

    // Clear the last full visible line
    uint32_t start_of_last_line = (screen_height - line_height) * pitch_in_pixels;
    for (uint32_t i = 0; i < line_size; i++) {
        framebuffer[start_of_last_line + i] = bg_color;
    }
}

void put_char(char c, uint32_t font_color, uint32_t bg_color) {
    if (c == '\n') {
        // Move to the next line
        cursor_x = 0;
        cursor_y += line_height;

        // Scroll if at the bottom of the screen
        if (cursor_y >= fb_info.height) {
            scroll_screen(bg_color);
            cursor_y -= line_height; // Adjust for scrolled line
        }
        return;
    }

    if (cursor_x >= fb_info.width) {
        // Move to the next line if at the end of the screen
        cursor_x = 0;
        cursor_y += line_height;

        // Scroll if at the bottom of the screen
        if (cursor_y >= fb_info.height) {
            scroll_screen(bg_color);
            cursor_y -= line_height; // Adjust for scrolled line
        }
    }

    // Draw the character at the current position
    draw_char(cursor_x, cursor_y, c, font_color);

    // Advance cursor
    cursor_x += 8; // Font width is 8 pixels
}

void fb_write_char(char ch) {
    put_char(ch, txt_color, bg_color);
}
