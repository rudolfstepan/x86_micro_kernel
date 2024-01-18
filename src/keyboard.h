#ifndef KEYBOARD_H    /* This is an "include guard" */
#define KEYBOARD_H 

#define KEYBOARD_PORT 0x60
#define KEY_RELEASED_PREFIX 0xF0
// Lookup table for scancode to ASCII character
#define SC_MAX 59  // Adjust the maximum to include the space character

#define LEFT_SHIFT_PRESSED 0x2A
#define LEFT_SHIFT_RELEASED 0xAA
#define RIGHT_SHIFT_PRESSED 0x36
#define RIGHT_SHIFT_RELEASED 0xB6
#define CAPS_LOCK_PRESSED 0x3A
#define CAPS_LOCK_RELEASED 0xBA
#define ENTER_PRESSED 0x1C
#define BACKSPACE_PRESSED 0x0E

#define BUFFER_SIZE 256


// unsigned char get_scancode_from_keyboard();
// unsigned char get_key_press();
// char scancode_to_ascii(unsigned char scancode);

void kb_install();

#endif