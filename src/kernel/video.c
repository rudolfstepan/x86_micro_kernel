
#include "video.h"
#include "io.h"
#include "../toolchain/strings.h"


unsigned short* vga_buffer = (unsigned short*)VGA_ADDRESS;


char current_color = 0x0F; // White on black background

void set_color(char color) {
    current_color = color;
}

// clear the screen
void clear_screen() {
    unsigned char color = 0x0F;  // Black background, white foreground
    for (int y = 0; y < VGA_ROWS; y++) {
        for (int x = 0; x < VGA_COLS; x++) {
            const int index = y * VGA_COLS + x;
            vga_buffer[index] = ' ' | (color << 8);
        }
    }
    int cursor_x = 0;
    int cursor_y = 0;

    set_cursor_position(cursor_x, cursor_y);
}

// get the cursor position
void get_cursor_position(int *x, int *y) {
    unsigned short position;

    // Read from Cursor Location High Register
    outb(VGA_CTRL_REGISTER, 0x0E);
    position = inb(VGA_DATA_REGISTER) << 8; // Shift to high byte

    // Read from Cursor Location Low Register
    outb(VGA_CTRL_REGISTER, 0x0F);
    position += inb(VGA_DATA_REGISTER); // Low byte

    // Calculate x and y from position
    *x = position % 80;
    *y = position / 80;
}

// set the cursor position
void set_cursor_position(int x, int y) {
    // The screen is 80 characters wide...
    unsigned short position = (y * 80) + x;

    // Cursor LOW port to VGA INDEX register
    outb(VGA_CTRL_REGISTER, 0x0F);
    outb(VGA_DATA_REGISTER, (unsigned char)(position & 0xFF));

    // Cursor HIGH port to VGA INDEX register
    outb(VGA_CTRL_REGISTER, 0x0E);
    outb(VGA_DATA_REGISTER, (unsigned char)((position >> 8) & 0xFF));
}

// write a character to the screen
void vga_write_char(char ch) {
    int cursor_x = 0;
    int cursor_y = 0;

    // update cursor position from hardware
    // this is necessary because the cursor position can change without our code knowing
    // when other code writes to the screen
    int current_x = cursor_x;
    int current_y = cursor_y;
    get_cursor_position(&current_x, &current_y);
    cursor_x = current_x;
    cursor_y = current_y;

    if (ch == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (ch == '\r') {
        cursor_x = 0;
    } else {
        const unsigned int index = cursor_y * VGA_COLS + cursor_x;
        vga_buffer[index] = (unsigned short)ch | (current_color << 8);  // 0x0F for white-on-black text
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
            vga_buffer[(VGA_ROWS - 1) * VGA_COLS + x] = ' ' | (current_color << 8);
        }

        cursor_y = VGA_ROWS - 1;  // Reset cursor to the last line
    }

    // Update hardware cursor to match our cursor position
    set_cursor_position(cursor_x, cursor_y);
}

// delete a character from the screen and move the cursor back to the previous position
void vga_backspace() {
    int cursor_x, cursor_y;
    get_cursor_position(&cursor_x, &cursor_y);

    // Move cursor one position to the left
    if (cursor_x == 0 && cursor_y > 0) {
        cursor_y--;
        cursor_x = VGA_COLS - 1; // Move to the end of the previous line
    } else if (cursor_x > 0) {
        cursor_x--;
    }

    // Update the cursor position
    set_cursor_position(cursor_x, cursor_y);

    // Erase the character at this new position by writing a space character
    const unsigned int index = cursor_y * VGA_COLS + cursor_x;
    vga_buffer[index] = ' ' | (current_color << 8);  // Assuming 0x0F for white-on-black text
}
