#ifndef DISPLAY_H
#define DISPLAY_H

// Display abstraction layer - works with both VGA text mode and framebuffer

// VGA color codes (used by both VGA and framebuffer for compatibility)
#define WHITE 0x0F
#define BLACK 0x00
#define BLUE 0x01
#define GREEN 0x02
#define CYAN 0x03
#define RED 0x04
#define YELLOW 0x0E
#define MAGENTA 0x05
#define BROWN 0x06
#define GRAY 0x07
#define LIGHT_GRAY 0x08
#define LIGHT_BLUE 0x09
#define LIGHT_GREEN 0x0A
#define LIGHT_CYAN 0x0B
#define LIGHT_RED 0x0C
#define LIGHT_MAGENTA 0x0D
#define LIGHT_BROWN 0x0E
#define BLINK 0x80
#define BRIGHT 0x08
#define UNDERLINE 0x01
#define DARK_GRAY 0x07

// Initialize display (auto-detects VGA or framebuffer)
void display_init();

// Clear the display
void display_clear();

// Write a character
void display_putchar(char c);

// Write a string
void display_write(const char* str);

// Get/set cursor position
void display_get_cursor(int* x, int* y);
void display_set_cursor(int x, int y);

// Backspace
void display_backspace();

// Set color (VGA colors for compatibility)
void display_set_color(char color);

#endif // DISPLAY_H
