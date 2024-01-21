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
#include "../toolchain/stdio.h"
#include "../toolchain/stdlib.h"

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
volatile bool enter_pressed = false;


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
    unsigned char key = 0;

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

void wait_for_enter() {
    printf("Press Enter to continue...\n");

    // Reset the enter_pressed flag
    enter_pressed = 0;

    // Wait in a loop until the Enter key is pressed
    while (!enter_pressed) {
        // Optionally, add a small delay here to prevent this loop
        // from consuming too much CPU
    }

    // Optional: Clear the input buffer after Enter is pressed
    buffer_index = 0;
    input_buffer[0] = '\0';
}
