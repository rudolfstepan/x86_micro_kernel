#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include "kernel/multiboot.h"

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

#endif // FRAMEBUFFER_H
