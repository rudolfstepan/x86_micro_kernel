#ifndef VIDEO_H    /* This is an "include guard" */
#define VIDEO_H    /* prevents the file from being included twice. */

#define VGA_ADDRESS 0xB8000         // VGA text mode address
#define VGA_CTRL_REGISTER 0x3D4     // VGA control register
#define VGA_DATA_REGISTER 0x3D5     // VGA data register

#define VGA_COLS 80
#define VGA_ROWS 25


// Colors
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


// Function prototypes
extern void set_color(char color);
extern void vga_backspace();
extern void vga_write_char(char ch);
extern void clear_screen();
extern void get_cursor_position(int *x, int *y);
extern void set_cursor_position(int x, int y);


#endif /* VIDEO_H */