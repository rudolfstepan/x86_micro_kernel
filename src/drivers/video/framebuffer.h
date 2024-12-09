#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include "kernel/multiboot.h"

// Colors (0xRRGGBB format)
#define BLACK        0x000000  // Black
#define BLUE         0x0000FF  // Blue
#define GREEN        0x00FF00  // Green
#define CYAN         0x00FFFF  // Cyan
#define RED          0xFF0000  // Red
#define MAGENTA      0xFF00FF  // Magenta
#define BROWN        0x8B4513  // Brown
#define GRAY         0x808080  // Gray
#define LIGHT_GRAY   0xD3D3D3  // Light Gray
#define LIGHT_BLUE   0x87CEFA  // Light Blue
#define LIGHT_GREEN  0x90EE90  // Light Green
#define LIGHT_CYAN   0xE0FFFF  // Light Cyan
#define LIGHT_RED    0xFFA07A  // Light Red
#define LIGHT_MAGENTA 0xFF77FF // Light Magenta
#define YELLOW       0xFFFF00  // Yellow
#define WHITE        0xFFFFFF  // White

// Special Attributes (optional for effects)
#define BLINK        0x80      // Placeholder for blinking (implementation-dependent)
#define BRIGHT       0x08      // Placeholder for brightness (implementation-dependent)


typedef struct {
    uint64_t address;        // Framebuffer physical address
    uint32_t pitch;          // Bytes per scanline
    uint32_t width;          // Screen width
    uint32_t height;         // Screen height
    uint8_t bpp;             // Bits per pixel
} framebuffer_info_t;


void parse_framebuffer(multiboot2_tag_framebuffer_t *fb);
void render_gradient();
void clear_screen();
void fill_screen(uint32_t color);

void clear_line(uint32_t y, uint32_t color);
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_char(uint32_t x, uint32_t y, char c, uint32_t color);
void draw_string(const char *str, uint32_t color, uint32_t bg_color);

void put_char(char c, uint32_t color, uint32_t bg_color);
void fb_write_char(char ch);
void set_color(uint32_t color);
void set_bg_color(uint32_t color);
void set_cursor_position(uint32_t x, uint32_t y);

void fb_backspace();
void fb_color_test();

#endif // FRAMEBUFFER_H
