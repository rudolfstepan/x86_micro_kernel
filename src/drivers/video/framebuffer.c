#include "framebuffer.h"
#include "kernel/multiboot.h"
#include "vga_font.h"


framebuffer_info_t fb_info;

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

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_info.width || y >= fb_info.height) {
        return; // Out of bounds
    }

    uint32_t *framebuffer = (uint32_t *)fb_info.address;
    framebuffer[(y * fb_info.pitch / 4) + x] = color;
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

void fill_screen(uint32_t color) {
    for (uint32_t y = 0; y < fb_info.height; y++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            draw_pixel(x, y, color);
        }
    }
}

void put_char(char c, uint32_t x, uint32_t y, uint32_t color) {
    // Shift index if the font starts at 0x20 (space)
    if (c < 0x20 || c > 0x7E) {
        c = '?'; // Default to '?' for unsupported characters
    }

    uint8_t *char_bitmap = vga_font[c + 2]; // Adjust for font array starting at 0x20
    uint32_t *framebuffer = (uint32_t *)fb_info.address;

    for (uint8_t row = 0; row < 16; row++) { // Font height is 16
        uint8_t row_data = char_bitmap[row];
        for (uint8_t col = 0; col < 8; col++) { // Font width is 8
            if (row_data & (1 << (7 - col))) { // Foreground pixel
                uint32_t pixel_x = x + col;
                uint32_t pixel_y = y + row;

                if (pixel_x < fb_info.width && pixel_y < fb_info.height) {
                    framebuffer[(pixel_y * fb_info.pitch / 4) + pixel_x] = color;
                }
            }
        }
    }
}

void draw_string(uint32_t x, uint32_t y, const char *str, uint32_t color) {
    uint32_t original_x = x; // Save the starting x-coordinate

    while (*str) {
        if (*str == '\n') {
            // Handle newline: Reset x to original position and move y down
            x = original_x;
            y += 16; // Advance by the font height (16 pixels for an 8x16 font)
        } else {
            // Draw the character and move to the next position
            put_char(*str, x, y, color);
            x += 8; // Advance by the font width (8 pixels for an 8x16 font)
        }
        str++;
    }
}
