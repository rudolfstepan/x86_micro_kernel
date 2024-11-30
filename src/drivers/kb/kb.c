#include "kb.h"
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

#define INPUT_QUEUE_SIZE 256
volatile char input_queue[INPUT_QUEUE_SIZE];
int input_queue_head = 0;
int input_queue_tail = 0;

// Funktion: Einfügen eines Zeichens in die Warteschlange
void input_queue_push(char ch) {
    int next_tail = (input_queue_tail + 1) % INPUT_QUEUE_SIZE;
    if (next_tail != input_queue_head) { // Überlauf vermeiden
        input_queue[input_queue_tail] = ch;
        input_queue_tail = next_tail;
    }
}

// Funktion: Entfernen eines Zeichens aus der Warteschlange
char input_queue_pop() {
    if (input_queue_head == input_queue_tail) {
        return '\0'; // Warteschlange ist leer
    }
    char ch = input_queue[input_queue_head];
    input_queue_head = (input_queue_head + 1) % INPUT_QUEUE_SIZE;
    return ch;
}

// Funktion: Entfernt das letzte Zeichen aus der Warteschlange
char input_queue_remove_last() {
    if (input_queue_head == input_queue_tail) {
        return '\0'; // Warteschlange ist leer
    }

    // Berechne die vorherige Position von input_queue_tail
    int prev_tail = (input_queue_tail - 1 + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;

    // Speichere das letzte Zeichen zur Rückgabe
    char last_char = input_queue[prev_tail];

    // Aktualisiere den Tail-Zeiger
    input_queue_tail = prev_tail;

    printf("input_queue_remove_last: %c\n", last_char);

    return last_char; // Gib das entfernte Zeichen zurück
}

// Funktion: Prüfen, ob die Warteschlange leer ist
bool input_queue_empty() {
    return input_queue_head == input_queue_tail;
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

                input_queue_push('\b'); // Add backspace to the queue
            }
        } else if (scan == ENTER_PRESSED) { // Handle Enter key
            input_queue_push('\n');
            buffer_index = 0;
            enter_pressed = true;               // Set Enter flag
        } else if (buffer_index < BUFFER_SIZE - 1) { // Regular key
            char key = scancode_to_ascii(scan, shift_pressed, caps_lock_active);
            if (key) {
                buffer_index++;
                input_queue_push(key);
                //putchar(key); // Echo the character
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
    char ch = input_queue_pop(); //input_buffer[0];

    // Re-enable interrupts
    enable_interrupts();

    return ch;
}

void get_input_line(char* buffer, int max_len) {
    int index = 0;

    while (1) {
        if (!input_queue_empty()) {
            char ch = input_queue_pop();

            if (ch == '\n') {
                buffer[index] = '\0'; // Null-terminieren
                return;
            }

            if (index < max_len - 1) {
                buffer[index++] = ch;
            }
        }
        // Task-Yield aufrufen, um andere Tasks auszuführen
        asm volatile("int $0x20"); // Timer-Interrupt auslösen
    }
}

void kb_install() {
    // Install the IRQ handler for the keyboard
    syscall(SYS_INSTALL_IRQ, (void*)1, (void*)kb_handler, 0);
    //memset(input_buffer, 0, sizeof(input_buffer));
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
}
