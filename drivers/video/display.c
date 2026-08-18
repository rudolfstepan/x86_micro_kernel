/**
 * @file drivers/video/display.c
 * @brief Dispatch zwischen VGA-Textmodus und Framebuffer.
 *
 * Layer: Ring-0 display driver.
 * Contract: Hardwarezustand und Pufferbereiche werden vor sichtbaren Seiteneffekten geprüft.
 * Safety: Nicht initialisierte Backends erhalten keine Ausgabe.
 */
#include "display.h"

#ifdef USE_FRAMEBUFFER
#include "framebuffer.h"
#include "drivers/char/serial.h"
#endif
#include "video.h"

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
    } else {
        clear_screen();
    }
#else
    // VGA text mode
    clear_screen();
#endif
}

void display_clear() {
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) framebuffer_clear();
    else clear_screen();
#else
    clear_screen();
#endif
}

void display_putchar(char c) {
    if (c == '\b') {
        display_backspace();
        return;
    }
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) {
        framebuffer_putchar(c);
        serial_write_char(SERIAL_COM1, c);
    } else {
        vga_write_char(c);
    }
#else
    vga_write_char(c);
#endif
}

void display_write(const char* str) {
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) {
        framebuffer_write_string(str);
        serial_write_string(SERIAL_COM1, str);
    } else while (*str) vga_write_char(*str++);
#else
    while (*str) {
        vga_write_char(*str++);
    }
#endif
}

void display_get_cursor(int* x, int* y) {
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) framebuffer_get_cursor(x, y);
    else get_cursor_position(x, y);
#else
    get_cursor_position(x, y);
#endif
}

void display_set_cursor(int x, int y) {
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) framebuffer_set_cursor(x, y);
    else set_cursor_position(x, y);
#else
    set_cursor_position(x, y);
#endif
}

void display_write_at(int x, int y, const char* text, unsigned int length) {
    if (!text) return;
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) {
        framebuffer_set_cursor(x, y);
        for (unsigned int index = 0; index < length; ++index)
            framebuffer_putchar(text[index]);
    } else {
        vga_write_at(x, y, text, length);
    }
#else
    vga_write_at(x, y, text, length);
#endif
}

void display_backspace() {
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) {
        framebuffer_putchar('\b');
        serial_write_char(SERIAL_COM1, '\b');
    }
    else vga_backspace();
#else
    vga_backspace();
#endif
}

void display_set_color(char color) {
#ifdef USE_FRAMEBUFFER
    if (framebuffer_available()) {
        uint32_t fg = vga_to_fb_color(color & 0x0F);
        uint32_t bg = vga_to_fb_color((color >> 4) & 0x0F);
        framebuffer_set_color(fg, bg);
    } else {
        set_color(color);
    }
#else
    set_color(color);
#endif
}
