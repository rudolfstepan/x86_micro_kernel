#include "keyboard.h"
#include "drivers/io/io.h"
#include "drivers/video/video.h"
#include "kernel/sys.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Constants and global variables
#define SC_MAX 59
#define BUFFER_SIZE 128
#define KEYBOARD_PORT 0x60

#define KEYBOARD_PORT 0x60
#define KEY_RELEASED_PREFIX 0xF0
// Lookup table for scancode to ASCII character
//#define SC_MAX 59  // Adjust the maximum to include the space character

#define LEFT_SHIFT_PRESSED 0x2A
#define LEFT_SHIFT_RELEASED 0xAA
#define RIGHT_SHIFT_PRESSED 0x36
#define RIGHT_SHIFT_RELEASED 0xB6
#define CAPS_LOCK_PRESSED 0x3A
#define CAPS_LOCK_RELEASED 0xBA
#define ENTER_PRESSED 0x1C
#define BACKSPACE_PRESSED 0x0E


const char scancode_to_char[SC_MAX] = {
    0,  0,  '1', '2', '3', '4', '5', '6', '7', '8',  /* 9 */
    '9', '0', '-', '=', 0,   0,  'Q', 'W', 'E', 'R', /* 19 */
    'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0,   0,  /* 29 */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', /* 39 */
    '\'', '`', 0,  '\\', 'Z', 'X', 'C', 'V', 'B', 'N', /* 49 */
    'M', ',', '.', '/', 0,   '*', 0,   ' ', 0         /* 58 */
};

char input_buffer[BUFFER_SIZE];
int shift_pressed = 0;
int buffer_index = 0;
volatile bool enter_pressed = false;

// Helper functions for atomic operations
bool is_enter_pressed() {
    // Atomically return the state of `enter_pressed`
    bool pressed = enter_pressed;
    return pressed;
}

void reset_enter_pressed() {
    // Atomically reset the `enter_pressed` flag
    enter_pressed = false;
}

unsigned char get_scancode_from_keyboard() {
    return inb(KEYBOARD_PORT);
}

char scancode_to_ascii(unsigned char scancode) {
    if (scancode > SC_MAX) {
        return 0; // Scancode out of range
    }
    return scancode_to_char[scancode];
}

void kb_handler(void* r) {
    unsigned char scan = get_scancode_from_keyboard();

    // Check if it's a key press event
    if (!(scan & 0x80)) {
        if (scan == 0x2A || scan == 0x36) { // Shift key pressed
            shift_pressed = 1;
        } else if (scan == 0x0E) { // Backspace
            if (buffer_index > 0) {
                buffer_index--;
                input_buffer[buffer_index] = '\0';
                vga_backspace();
            }
        } else if (scan == 0x1C) { // Enter key
            input_buffer[buffer_index++] = '\0'; // Null-terminate
            enter_pressed = true;
        } else {
            char key = scancode_to_ascii(scan);
            if (shift_pressed && key >= 'a' && key <= 'z') {
                key -= 32; // Convert to uppercase
            }
            input_buffer[buffer_index++] = key;
            vga_write_char(key);
        }
    } else {
        // Key release event
        if (scan == 0xAA || scan == 0xB6) { // Shift released
            shift_pressed = 0;
        }
    }

    // Send End of Interrupt (EOI) signal to the PIC
    outb(0x20, 0x20);
}

void kb_install() {
    memset(input_buffer, 0, sizeof(input_buffer));
    // irq_install_handler(1, kb_handler); // assumed to be set up elsewhere
}

void kb_wait_enter() {
    printf("Press Enter to continue...\n");

    // Reset the enter_pressed flag
    reset_enter_pressed();

    // Wait until the Enter key is pressed
    while (!is_enter_pressed()) {
        // Small delay to avoid busy-waiting and CPU overuse
        for (volatile int i = 0; i < 100000; i++);
    }

    // Clear the input buffer after Enter is pressed
    buffer_index = 0;
    input_buffer[0] = '\0';
}
