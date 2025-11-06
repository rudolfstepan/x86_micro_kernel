#include "display.h"

#ifdef USE_FRAMEBUFFER
#include "framebuffer.h"
#else
#include "video.h"
#endif

// VGA color to framebuffer color mapping
#ifdef USE_FRAMEBUFFER
static uint32_t vga_to_fb_color(char vga_color) {
    uint32_t color_map[16] = {
        FB_COLOR_BLACK,        // 0x00
        FB_COLOR_BLUE,         // 0x01
        FB_COLOR_GREEN,        // 0x02
        FB_COLOR_CYAN,         // 0x03
        FB_COLOR_RED,          // 0x04
        FB_COLOR_MAGENTA,      // 0x05
        FB_COLOR_BROWN,        // 0x06
        FB_COLOR_LIGHT_GRAY,   // 0x07
        FB_COLOR_DARK_GRAY,    // 0x08
        FB_COLOR_LIGHT_BLUE,   // 0x09
        FB_COLOR_LIGHT_GREEN,  // 0x0A
        FB_COLOR_LIGHT_CYAN,   // 0x0B
        FB_COLOR_LIGHT_RED,    // 0x0C
        FB_COLOR_LIGHT_MAGENTA,// 0x0D
        FB_COLOR_YELLOW,       // 0x0E
        FB_COLOR_WHITE         // 0x0F
    };
    return color_map[vga_color & 0x0F];
}
#endif

void display_init() {
#ifdef USE_FRAMEBUFFER
    // Framebuffer will be initialized from multiboot info in kernel
    // For now, just clear if available
    if (framebuffer_available()) {
        framebuffer_clear();
    }
#else
    // VGA text mode
    clear_screen();
#endif
}

void display_clear() {
#ifdef USE_FRAMEBUFFER
    framebuffer_clear();
#else
    clear_screen();
#endif
}

void display_putchar(char c) {
#ifdef USE_FRAMEBUFFER
    framebuffer_putchar(c);
#else
    vga_write_char(c);
#endif
}

void display_write(const char* str) {
#ifdef USE_FRAMEBUFFER
    framebuffer_write_string(str);
#else
    while (*str) {
        vga_write_char(*str++);
    }
#endif
}

void display_get_cursor(int* x, int* y) {
#ifdef USE_FRAMEBUFFER
    framebuffer_get_cursor(x, y);
#else
    get_cursor_position(x, y);
#endif
}

void display_set_cursor(int x, int y) {
#ifdef USE_FRAMEBUFFER
    framebuffer_set_cursor(x, y);
#else
    set_cursor_position(x, y);
#endif
}

void display_backspace() {
#ifdef USE_FRAMEBUFFER
    framebuffer_putchar('\b');
#else
    vga_backspace();
#endif
}

void display_set_color(char color) {
#ifdef USE_FRAMEBUFFER
    uint32_t fg = vga_to_fb_color(color & 0x0F);
    uint32_t bg = vga_to_fb_color((color >> 4) & 0x0F);
    framebuffer_set_color(fg, bg);
#else
    set_color(color);
#endif
}
