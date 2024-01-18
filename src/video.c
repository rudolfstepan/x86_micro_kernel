
#include "video.h"
#include "io.h"
#include "strings.h"


unsigned short* vga_buffer = (unsigned short*)VGA_ADDRESS;
unsigned int cursor_x;
unsigned int cursor_y;

void update_cursor(int x, int y) {
    // The screen is 80 characters wide...
    unsigned short position = (y * 80) + x;

    // Cursor LOW port to VGA INDEX register
    outb(VGA_CTRL_REGISTER, 0x0F);
    outb(VGA_DATA_REGISTER, (unsigned char)(position & 0xFF));

    // Cursor HIGH port to VGA INDEX register
    outb(VGA_CTRL_REGISTER, 0x0E);
    outb(VGA_DATA_REGISTER, (unsigned char)((position >> 8) & 0xFF));

    cursor_x = x;
    cursor_y = y;
}

void clear_screen() {
    unsigned char color = 0x0F;  // Black background, white foreground
    for (int y = 0; y < VGA_ROWS; y++) {
        for (int x = 0; x < VGA_COLS; x++) {
            const int index = y * VGA_COLS + x;
            vga_buffer[index] = ' ' | (color << 8);
        }
    }
    cursor_x = 0;
    cursor_y = 0;

    update_cursor(cursor_x, cursor_y);
}

void vga_write_char(char ch) {
    if (ch == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (ch == '\r') {
        cursor_x = 0;
    } else {
        const unsigned int index = cursor_y * VGA_COLS + cursor_x;
        vga_buffer[index] = (unsigned short)ch | (0x0F << 8);  // 0x0F for white-on-black text
        cursor_x++;
        if (cursor_x >= VGA_COLS) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_ROWS) {
        for (int y = 0; y < VGA_ROWS - 1; y++) {
            for (int x = 0; x < VGA_COLS; x++) {
                vga_buffer[y * VGA_COLS + x] = vga_buffer[(y + 1) * VGA_COLS + x];
            }
        }

        // Clear the last line
        for (int x = 0; x < VGA_COLS; x++) {
            vga_buffer[(VGA_ROWS - 1) * VGA_COLS + x] = ' ' | (0x0F << 8);
        }

        cursor_y = VGA_ROWS - 1;  // Reset cursor to the last line
    }

    update_cursor(cursor_x, cursor_y);
}

void vga_backspace() {
    if (cursor_x == 0 && cursor_y > 0) {
        cursor_x = VGA_COLS - 1;
        cursor_y--;
    } else if (cursor_x > 0) {
        cursor_x--;
    }

    // Write a space at the current cursor position
    vga_write_char(' ');

    // Move the cursor back to the position where the space was written
    if (cursor_x == 0 && cursor_y > 0) {
        cursor_x = VGA_COLS - 1;
        cursor_y--;
    } else if (cursor_x > 0) {
        cursor_x--;
    }

    update_cursor(cursor_x, cursor_y);
}

void vga_move_cursor_left() {
    if (cursor_x > 0) {
        cursor_x--;
    }
    update_cursor(cursor_x, cursor_y);
}
