#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>

// Framebuffer configuration
#define FB_WIDTH 1024
#define FB_HEIGHT 768
#define FB_BPP 32  // Bits per pixel (32-bit color)

// Multiboot framebuffer info structure
typedef struct {
    uint32_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
} multiboot_framebuffer_info_t;

// Font dimensions
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

// Terminal dimensions based on framebuffer and font
#define TERM_COLS (FB_WIDTH / FONT_WIDTH)
#define TERM_ROWS (FB_HEIGHT / FONT_HEIGHT)

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
void framebuffer_clear();
void framebuffer_putchar(char c);
void framebuffer_write_string(const char* str);
void framebuffer_set_color(uint32_t fg, uint32_t bg);
void framebuffer_scroll();
void framebuffer_get_cursor(int* x, int* y);
void framebuffer_set_cursor(int x, int y);

// Check if framebuffer is available
bool framebuffer_available();

#endif // FRAMEBUFFER_H
