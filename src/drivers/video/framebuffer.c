#include "framebuffer.h"
#include "kernel/multiboot.h"
#include "vga_font.h"
#include "toolchain/stdio.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


uint32_t cursor_x = 0;
uint32_t cursor_y = 0;
uint32_t line_height = 16; // Height of a single text line (e.g., 8x16 font)
uint32_t left_padding = 8; // Padding on the left border

static uint32_t txt_color = WHITE;
static uint32_t bg_color = BLACK;
static framebuffer_info_t fb_info;

void set_color(uint32_t color) {
    txt_color = color;
}

void set_bg_color(uint32_t color) {
    bg_color = color;
}

void set_cursor_position(uint32_t x, uint32_t y) {
    cursor_x = x + left_padding;
    cursor_y = y;
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

    set_color(YELLOW);
    printf("Fb Info: ");
    printf("Address: 0x%X ", fb_info.address);
    printf("Res: %ux%u ", fb_info.width, fb_info.height);
    printf("BPP: %u ", fb_info.bpp);
    printf("Pitch: %u bytes\n", fb_info.pitch);
    set_color(WHITE);
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t font_color) {
    uint64_t framebuffer_address = fb_info.address;
    uint32_t *framebuffer = (uint32_t *)(uintptr_t)framebuffer_address;

    for (uint8_t row = 0; row < 16; row++) {
        uint8_t row_data = font[(uint8_t)c + 2][row];
        for (uint8_t col = 0; col < 8; col++) {
            if (row_data & (1 << (7 - col))) {
                framebuffer[((y + row) * fb_info.pitch / 4) + (x + col)] = font_color;
            }
        }
    }
}

// void draw_string(const char *str, uint32_t font_color, uint32_t bg_color) {
//     while (*str) {
//         put_char(*str++, font_color, bg_color);
//     }
// }

void draw_pixel(uint32_t x, uint32_t y, uint32_t font_color) {
    if (x >= fb_info.width || y >= fb_info.height) {
        return; // Out of bounds
    }

    uint64_t framebuffer_address = fb_info.address;
    uint32_t *framebuffer = (uint32_t *)(uintptr_t)framebuffer_address;
    framebuffer[(y * fb_info.pitch / 4) + x] = font_color;
}

void draw_rect(int x, int y, int width, int height, uint32_t color) {
    for (int dx = 0; dx < width; dx++) {
        for (int dy = 0; dy < height; dy++) {
            draw_pixel(x + dx, y + dy, color);
        }
    }
}

void clear_screen() {
    uint64_t framebuffer_address = fb_info.address;
    uint32_t *framebuffer = (uint32_t *)(uintptr_t)framebuffer_address;
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
    uint64_t framebuffer_address = fb_info.address;
    uint32_t *framebuffer = (uint32_t *)(uintptr_t)framebuffer_address;
    for (uint32_t row = 0; row < line_height; row++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            framebuffer[((y + row) * fb_info.pitch / 4) + x] = color;
        }
    }
}

void scroll_screen(uint32_t bg_color) {
    uint64_t framebuffer_address = fb_info.address;
    uint32_t *framebuffer = (uint32_t *)(uintptr_t)framebuffer_address;
    uint32_t screen_width = fb_info.width;
    uint32_t screen_height = fb_info.height;
    uint32_t pitch_in_pixels = fb_info.pitch / 4; // Convert pitch from bytes to pixels

    uint32_t total_pixels_to_move = (screen_height - line_height) * pitch_in_pixels; // Total pixels to move
    uint32_t start_of_clear_area = (screen_height - line_height) * pitch_in_pixels;  // Start of the lines to clear
    uint32_t pixels_per_line = screen_width; // Pixels per line

    // Move lines up (forward direction to handle overlap safely)
    __asm__ volatile (
        "cld\n"                     // Clear Direction Flag (forward direction)
        "rep movsd\n"               // Move double words (4 bytes)
        :
        : "D"(framebuffer),         // Destination (start of the screen)
          "S"(framebuffer + line_height * pitch_in_pixels), // Source (start of the lines to scroll up)
          "c"(total_pixels_to_move) // Number of double words to move
        : "memory"                  // Mark memory as clobbered
    );

    // Clear the last lines
    __asm__ volatile (
        "cld\n"                     // Clear Direction Flag (forward direction)
        "rep stosl\n"               // Store double words (4 bytes)
        :
        : "D"(framebuffer + start_of_clear_area), // Destination address (start of last lines)
          "a"(bg_color),            // Value to fill (background color)
          "c"(line_height * pitch_in_pixels) // Number of double words to store
        : "memory"                  // Mark memory as clobbered
    );
}

void put_char(char c, uint32_t font_color, uint32_t bg_color) {
    if (c == '\n') {
        cursor_x = left_padding;
        cursor_y += line_height;

        // Scroll if at the bottom of the screen
        if (cursor_y >= fb_info.height - line_height) {
            scroll_screen(bg_color);
            cursor_y = fb_info.height - line_height; // Reset to the last visible line
        }
        return;
    }

    if (cursor_x >= fb_info.width) {
        cursor_x = left_padding;
        cursor_y += line_height;

        if (cursor_y >= fb_info.height - line_height) {
            scroll_screen(bg_color);
            cursor_y = fb_info.height - line_height; // Reset to the last visible line
        }
    }

    draw_char(cursor_x, cursor_y, c, font_color);
    cursor_x += 8; // Advance cursor by font width
}

void fb_write_char(char ch) {
    put_char(ch, txt_color, bg_color);
}

void fb_backspace() {
    if (cursor_x == 0 && cursor_y > 0) {
        cursor_y -= line_height;
        cursor_x = fb_info.width - 8; // Move to the end of the previous line
    } else if (cursor_x > 0) {
        cursor_x -= 8;
    }

    // Clear the character at the current position
    for (uint8_t row = 0; row < 16; row++) {
        for (uint8_t col = 0; col < 8; col++) {
            draw_pixel(cursor_x + col, cursor_y + row, bg_color);
        }
    }
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

void fb_color_test() {
    uint64_t framebuffer_address = fb_info.address;
    uint32_t *framebuffer = (uint32_t *)(uintptr_t)framebuffer_address;
    uint32_t width = fb_info.width;
    uint32_t height = fb_info.height;
    uint32_t pitch_in_pixels = fb_info.pitch / 4;

    // Test 1: Vertical Gradient (RGB)
    for (uint32_t y = 0; y < height / 3; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t color = (y * 255 / (height / 3)) << 16; // Red gradient
            framebuffer[y * pitch_in_pixels + x] = color;
        }
    }

    for (uint32_t y = height / 3; y < (2 * height) / 3; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t color = ((y - height / 3) * 255 / (height / 3)) << 8; // Green gradient
            framebuffer[y * pitch_in_pixels + x] = color;
        }
    }

    for (uint32_t y = (2 * height) / 3; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t color = ((y - (2 * height) / 3) * 255 / (height / 3)); // Blue gradient
            framebuffer[y * pitch_in_pixels + x] = color;
        }
    }

    // Test 2: Horizontal Gradient (Brightness)
    for (uint32_t y = 0; y < height / 3; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t brightness = (x * 255 / width);
            uint32_t color = (brightness << 16) | (brightness << 8) | brightness; // Gray gradient
            framebuffer[(height / 3 + y) * pitch_in_pixels + x] = color;
        }
    }

    // Test 3: Color Blocks
    uint32_t block_size = width / 8;
    uint32_t colors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x808080, 0xFFFFFF };

    for (uint32_t i = 0; i < 8; i++) {
        for (uint32_t y = (2 * height) / 3; y < height; y++) {
            for (uint32_t x = i * block_size; x < (i + 1) * block_size; x++) {
                framebuffer[y * pitch_in_pixels + x] = colors[i];
            }
        }
    }
}

#pragma pack(push, 1) // Ensure structures are tightly packed
typedef struct {
    uint16_t signature;        // "BM" signature
    uint32_t file_size;        // Size of the BMP file
    uint32_t reserved;         // Reserved (unused)
    uint32_t pixel_data_offset; // Offset to the pixel data
} BMPFileHeader;

typedef struct {
    uint32_t header_size;      // Size of the DIB header
    int32_t width;             // Image width
    int32_t height;            // Image height
    uint16_t planes;           // Number of color planes
    uint16_t bpp;              // Bits per pixel
    uint32_t compression;      // Compression method
    uint32_t image_size;       // Size of the pixel data
    int32_t x_resolution;      // Horizontal resolution (unused)
    int32_t y_resolution;      // Vertical resolution (unused)
    uint32_t colors_used;      // Number of colors used (0 = all)
    uint32_t important_colors; // Important colors (0 = all)
} BMPDIBHeader;
#pragma pack(pop)

void display_bmp(void* buffer, uint32_t screen_x, uint32_t screen_y) {
    BMPFileHeader* file_header = (BMPFileHeader*)buffer;
    BMPDIBHeader* dib_header = (BMPDIBHeader*)(buffer + sizeof(BMPFileHeader));

    // Validate BMP signature
    if (file_header->signature != 0x4D42) { // 'BM'
        printf("Error: Invalid BMP file signature.\n");
        return;
    }

    // Extract image properties
    int32_t width = dib_header->width;
    int32_t height = dib_header->height;
    uint16_t bpp = dib_header->bpp;

    if (bpp != 24) { // Only support 24-bit BMP files
        printf("Error: Unsupported BMP format (only 24 BPP supported).\n");
        return;
    }

    // Row size aligned to a 4-byte boundary
    uint8_t* pixel_data = (uint8_t*)buffer + file_header->pixel_data_offset;
    uint32_t row_size = (width * 3 + 3) & ~3;

    // Debug output
    printf("BMP Info: Width=%d, Height=%d, BPP=%d, Row Size=%d bytes\n", width, height, bpp, row_size);

    // Draw the BMP pixel data (convert 24-bit BGR to 32-bit)
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Read BMP pixel (BGR format, row_size accounts for padding)
            uint8_t blue = pixel_data[y * row_size + x * 3];
            uint8_t green = pixel_data[y * row_size + x * 3 + 1];
            uint8_t red = pixel_data[y * row_size + x * 3 + 2];

            // Flip BMP row vertically: BMP stores pixels bottom-up
            int fb_x = screen_x + x;
            int fb_y = screen_y + (height - 1 - y);

            // Draw pixel using the draw_pixel method
            draw_pixel(fb_x, fb_y, (red << 16) | (green << 8) | blue);
        }
    }

    printf("BMP displayed successfully at (%u, %u).\n", screen_x, screen_y);
}
