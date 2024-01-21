#ifndef VIDEO_H    /* This is an "include guard" */
#define VIDEO_H    /* prevents the file from being included twice. */

#define VGA_ADDRESS 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTRIB 0x0F  // White text on black background

#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5

#define BLACK 0
#define GREEN 2
#define RED 4
#define YELLOW 14
#define WHITE_COLOR 15


void vga_backspace();
void vga_write_char(char ch);
void clear_screen();
void get_cursor_position(int *x, int *y);
void set_cursor_position(int x, int y);


#endif /* VIDEO_H */