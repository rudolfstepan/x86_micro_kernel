﻿#include "kb.h"
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

#define KEY_RELEASED_PREFIX 0xF0

#define LEFT_SHIFT_PRESSED 0x2A
#define LEFT_SHIFT_RELEASED 0xAA
#define RIGHT_SHIFT_PRESSED 0x36
#define RIGHT_SHIFT_RELEASED 0xB6
#define CAPS_LOCK_PRESSED 0x3A
#define ENTER_PRESSED 0x1C
#define BACKSPACE_PRESSED 0x0E

const char scancode_to_char[SC_MAX] = {
    0,  0,  '1', '2', '3', '4', '5', '6', '7', '8',  /* 9 */
    '9', '0', '-', '=', 0,   0,  'q', 'w', 'e', 'r', /* 19 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,  /* 29 */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 39 */
    '\'', '`', 0,  '\\', 'z', 'x', 'c', 'v', 'b', 'n', /* 49 */
    'm', ',', '.', '/', 0,   '*', 0,   ' ', 0         /* 58 */
};

const char scancode_to_char_shift[SC_MAX] = {
    0,  0,  '!', '@', '#', '$', '%', '^', '&', '*',  /* 9 */
    '(', ')', '_', '+', 0,   0,  'Q', 'W', 'E', 'R', /* 19 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,  /* 29 */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', /* 39 */
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', /* 49 */
    'M', '<', '>', '?', 0,   '*', 0,   ' ', 0         /* 58 */
};

char input_buffer[BUFFER_SIZE];
int shift_pressed = 0;
int caps_lock_active = 0;
int buffer_index = 0;
volatile bool enter_pressed = false;

// Helper functions for atomic operations
unsigned char get_scancode_from_keyboard() {
    return inb(KEYBOARD_PORT);
}

char scancode_to_ascii(unsigned char scancode, bool shift, bool caps_lock) {
    if (scancode >= SC_MAX) {
        return 0; // Scancode out of range
    }

    char key = shift ? scancode_to_char_shift[scancode] : scancode_to_char[scancode];

    if (caps_lock && key >= 'a' && key <= 'z') {
        key -= 32; // Convert lowercase to uppercase
    } else if (caps_lock && key >= 'A' && key <= 'Z' && !shift) {
        key += 32; // Convert uppercase to lowercase
    }

    return key;
}

void clear_input_buffer() {
    memset(input_buffer, 0, sizeof(input_buffer));
    buffer_index = 0;
}

void reset_enter_pressed() {
    // Atomically reset the `enter_pressed` flag
    enter_pressed = false;
}

// Helper functions for atomic operations
bool is_enter_pressed() {
    // Atomically return the state of `enter_pressed`
    bool pressed = enter_pressed;
    return pressed;
}

void kb_handler(void* r) {
    unsigned char scan = get_scancode_from_keyboard();

    // Key press event
    if (!(scan & 0x80)) {
        if (scan == LEFT_SHIFT_PRESSED || scan == RIGHT_SHIFT_PRESSED) {
            shift_pressed = 1;
        } else if (scan == CAPS_LOCK_PRESSED) {
            caps_lock_active = !caps_lock_active; // Toggle Caps Lock
        } else if (scan == BACKSPACE_PRESSED) { // Handle Backspace
            if (buffer_index > 0) {
                buffer_index--;
                input_buffer[buffer_index] = '\0';
                vga_backspace(); // Clear character on the screen (if implemented)
            }
        } else if (scan == ENTER_PRESSED) { // Handle Enter key
            //input_buffer[buffer_index++] = '\n'; // Add newline character
            input_buffer[buffer_index] = '\0';   // Null-terminate
            enter_pressed = true;               // Set Enter flag
        } else if (buffer_index < BUFFER_SIZE - 1) { // Regular key
            char key = scancode_to_ascii(scan, shift_pressed, caps_lock_active);
            if (key) {
                input_buffer[buffer_index++] = key;
                input_buffer[buffer_index] = '\0'; // Null-terminate buffer
                putchar(key); // Echo the character
            }
        }
    } else {
        // Key release event
        if (scan == LEFT_SHIFT_RELEASED || scan == RIGHT_SHIFT_RELEASED) {
            shift_pressed = 0;
        }
    }

    // Send End of Interrupt (EOI) signal to the PIC
    outb(0x20, 0x20);
}

// Get a character from the input buffer
// This function is blocking and waits for a character to be available
char getchar() {
    while (buffer_index == 0) {
        // Add a small delay to avoid busy-waiting completely
        sleep_ms(10);
    }

    // Disable interrupts to safely access the buffer
    disable_interrupts();

    // Retrieve the first character from the buffer
    char ch = input_buffer[0];

    // Shift the buffer to remove the consumed character
    for (int i = 1; i < buffer_index; i++) {
        input_buffer[i - 1] = input_buffer[i];
    }

    buffer_index--;

    // Re-enable interrupts
    enable_interrupts();

    return ch;
}

void get_input_line(char* buffer, int max_len) {
    // Reset the enter_pressed flag
    enter_pressed = false;

    while (1) {
        if (enter_pressed) {
            enter_pressed = false; // Reset for next input
            memcpy(buffer, input_buffer, buffer_index);

            // Null-terminate the buffer
            buffer[buffer_index] = '\0';

            // Clear the input buffer
            clear_input_buffer();

            return;
        }
    }
}

void kb_install() {
    // Install the IRQ handler for the keyboard
    irq_install_handler(1, kb_handler);
    
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
        sleep_ms(10);
    }

    // Clear the input buffer after Enter is pressed
    buffer_index = 0;
    input_buffer[0] = '\0';
}
