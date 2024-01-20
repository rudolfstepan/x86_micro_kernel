// --------------------------------------------------
// Author: Rudolf Stepan
// Version: 1.0
// Date: 18.1.2024
// Brief: Keyboard driver implementation
// --------------------------------------------------

// Includes
#include <stdbool.h>
#include "video.h"
#include "keyboard.h"  
#include "io.h"
#include "system.h"
#include "sys.h"
#include "memory.h"

const char scancode_to_char[SC_MAX] = {
    0,  0,  '1', '2', '3', '4', '5', '6', '7', '8',  /* 9 */
    '9', '0', '-', '=', 0,   0,  'Q', 'W', 'E', 'R', /* 19 */
    'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0,   0,  /* 29 */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', /* 39 */
    '\'', '`', 0,  '\\', 'Z', 'X', 'C', 'V', 'B', 'N', /* 49 */
    'M', ',', '.', '/', 0,   '*', 0,   ' ', 0         /* 58 */
};

const char scancode_to_char_shifted[SC_MAX] = {
    // Your character map for shifted keys
    0,  0,  '!', '"', '.', '$', '%', '&', '/', '(',  /* 9 */
    ')', '=', '-', '=', 0,   0,  'Q', 'W', 'E', 'R', /* 19 */
    'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0,   0,  /* 29 */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', /* 39 */
    '\'', '`', 0,  '\\', 'Z', 'X', 'C', 'V', 'B', 'N', /* 49 */
    'M', ',', '.', '/', 0,   '*', 0,   ' ', 0         /* 58 */
};


char input_buffer[BUFFER_SIZE];
int shift_pressed = 0;
int buffer_index = 0;
bool enter_pressed = false;


unsigned char get_scancode_from_keyboard() {
    return inb(KEYBOARD_PORT);
}

char scancode_to_ascii(unsigned char scancode) {
    if (scancode > SC_MAX) {
        return 0; // Scancode out of range of our lookup table
    }

    return scancode_to_char[scancode];
}

void kb_handler(struct regs* r) {
    (void)r; // Suppress unused parameter warning

    unsigned char scan;
    unsigned char key;

    scan = get_scancode_from_keyboard();

    // Check if it's a key press event (bit 7 of scan code set)
    if (!(scan & 0x80)) {
        // Key press event
        if (scan == 0x2A || scan == 0x36) {
            shift_pressed = 1;
        } else if (scan == 0x0E) { // Backspace key
            if (buffer_index > 0) {
                buffer_index--;
                input_buffer[buffer_index] = '\0';
                vga_backspace();
            }
        } else if (scan == 0x1C) { // Enter key
            input_buffer[buffer_index++] = '\0'; // Null-terminate the string
            enter_pressed = true;
        } else {
            key = scancode_to_ascii(scan);

            if (shift_pressed && key >= 'a' && key <= 'z') {
                key -= 32; // Convert to uppercase
            }

            input_buffer[buffer_index++] = key;
            vga_write_char(key);
        }
    } else {
        // Key release event
        if (scan == 0xAA || scan == 0xB6) { // Shift key released
            shift_pressed = 0;
        }
    }
}

void kb_install() {

    // Clear the input buffer
    memset(input_buffer, 0, sizeof(input_buffer));

    irq_install_handler(1, kb_handler);
}

// unsigned char get_key_press() {
//     static unsigned char last_scancode = 0;
//     unsigned char scancode = 0;

//     while (1) {
//         scancode = get_scancode_from_keyboard();

//         if (scancode == KEY_RELEASED_PREFIX) {
//             // Next read will be the released key
//             last_scancode = 0;
//         } else if (last_scancode != scancode) {
//             // New key press detected
//             last_scancode = scancode;
//             return scancode;
//         }
//         // If none of the above, it's a repeat or a release, so do nothing
//     }
// }

// unsigned char get_key_press() {
//     static int shift_pressed = 0;
//     static int ignore_next_scancode = 0;
//     unsigned char scancode = 0;

//     while (1) {
//         scancode = get_scancode_from_keyboard();

//         // Handle shift key press and release
//         if (scancode == LEFT_SHIFT_PRESSED || scancode == RIGHT_SHIFT_PRESSED) {
//             shift_pressed = 1;
//         } else if (scancode == LEFT_SHIFT_RELEASED || scancode == RIGHT_SHIFT_RELEASED) {
//             shift_pressed = 0;
//         } else if (scancode == KEY_RELEASED_PREFIX) {
//             ignore_next_scancode = 1;
//         } else if (ignore_next_scancode) {
//             ignore_next_scancode = 0;
//         } else if (shift_pressed) {
//             // Here we will combine the shift state with the scancode
//             return (scancode | 0x80); // Set high bit if shift is pressed
//         } else {
//             return scancode; // Return the unmodified scancode
//         }
//     }
// }

// unsigned char get_key_press() {
//     static unsigned char last_scancode = 0;
//     static int shift_pressed = 0;
//     static int ignore_next_scancode = 0;
//     static int repeat_state = 0;
//     unsigned char scancode = 0;

//     while (1) {
//         scancode = get_scancode_from_keyboard();

//         // Handle shift key press and release
//         if (scancode == LEFT_SHIFT_PRESSED || scancode == RIGHT_SHIFT_PRESSED) {
//             shift_pressed = 1;
//             continue;
//         } else if (scancode == LEFT_SHIFT_RELEASED || scancode == RIGHT_SHIFT_RELEASED) {
//             shift_pressed = 0;
//             continue;
//         }

//         // Check if this is a key release event
//         if (scancode == KEY_RELEASED_PREFIX) {
//             ignore_next_scancode = 1;
//             repeat_state = 0; // Reset repeat state on key release
//             continue;
//         }

//         if (ignore_next_scancode) {
//             ignore_next_scancode = 0;
//             last_scancode = 0; // Reset last_scancode on actual key release
//             continue;
//         }

//         // Handle new key press or repeat
//         if (scancode != last_scancode) {
//             last_scancode = scancode;
//             repeat_state = 1; // Set repeat state
//             if (shift_pressed) {
//                 scancode |= 0x80; // Use high bit to indicate shift is pressed
//             }
//             return scancode;
//         } else if (!repeat_state) {
//             // If it's a repeated scancode and repeat_state is not set, return it
//             repeat_state = 1;
//             if (shift_pressed) {
//                 scancode |= 0x80;
//             }
//             return scancode;
//         }
//         // If none of the above, it's a continuous repeat, so do nothing
//     }
// }


// char scancode_to_ascii(unsigned char scancode) {
//     int shifted = (scancode & 0x80) != 0;
//     scancode &= 0x7F; // Clear high bit

//     if (scancode > SC_MAX) {
//         return 0; // Scancode out of range
//     }

//     return shifted ? scancode_to_char_shifted[scancode] : scancode_to_char[scancode];
// }