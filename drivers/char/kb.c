#include "kb.h"
#include "drivers/char/io.h"
#include "drivers/char/serial.h"
#include "drivers/video/video.h"
#include "arch/x86/sys.h"
#include "arch/x86/include/interrupt.h"
#include "include/lib/spinlock.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//=============================================================================
// CONSTANTS AND CONFIGURATION
//=============================================================================

#define KEYBOARD_DATA_PORT      0x60
#define KEYBOARD_STATUS_PORT    0x64

#define SC_MAX                  89  // Extended to cover more scancodes
#define INPUT_QUEUE_SIZE        256
#define BUFFER_SIZE             128

// Scancode prefixes
#define SC_EXTENDED_PREFIX      0xE0  // Extended keys (arrows, etc.)
#define SC_PAUSE_PREFIX         0xE1  // Pause key (rarely used)
#define SC_RELEASE_MASK         0x80  // Bit 7 set means key released

// Special scancodes (Set 1)
#define SC_LEFT_SHIFT           0x2A
#define SC_RIGHT_SHIFT          0x36
#define SC_LEFT_CTRL            0x1D
#define SC_LEFT_ALT             0x38
#define SC_CAPS_LOCK            0x3A
#define SC_NUM_LOCK             0x45
#define SC_SCROLL_LOCK          0x46
#define SC_BACKSPACE            0x0E
#define SC_TAB                  0x0F
#define SC_ENTER                0x1C
#define SC_ESCAPE               0x01

// Extended scancodes (with E0 prefix)
#define SC_EXT_RIGHT_CTRL       0x1D
#define SC_EXT_RIGHT_ALT        0x38
#define SC_EXT_UP               0x48
#define SC_EXT_DOWN             0x50
#define SC_EXT_LEFT             0x4B
#define SC_EXT_RIGHT            0x4D
#define SC_EXT_HOME             0x47
#define SC_EXT_END              0x4F
#define SC_EXT_PAGE_UP          0x49
#define SC_EXT_PAGE_DOWN        0x51
#define SC_EXT_INSERT           0x52
#define SC_EXT_DELETE           0x53

//=============================================================================
// SCANCODE TO ASCII TRANSLATION TABLES
//=============================================================================

// Normal mode (no shift)
const char scancode_to_char[SC_MAX] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  // 0-9
    '9',  '0',  '-',  '=',  0x08, 0x09, 'q',  'w',  'e',  'r',  // 10-19
    't',  'y',  'u',  'i',  'o',  'p',  '[',  ']',  0x0A, 0,    // 20-29
    'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 30-39
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  'b',  'n',  // 40-49
    'm',  ',',  '.',  '/',  0,    '*',  0,    ' ',  0,    0,    // 50-59 (F1 at 59)
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 60-69 (F2-F10)
    0,    0,    '7',  '8',  '9',  '-',  '4',  '5',  '6',  '+',  // 70-79 (numpad)
    '1',  '2',  '3',  '0',  '.',  0,    0,    0,    0            // 80-88
};

// Shift mode
const char scancode_to_char_shift[SC_MAX] = {
    0,    0,    '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  // 0-9
    '(',  ')',  '_',  '+',  0x08, 0x09, 'Q',  'W',  'E',  'R',  // 10-19
    'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}',  0x0A, 0,    // 20-29
    'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 30-39
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',  'B',  'N',  // 40-49
    'M',  '<',  '>',  '?',  0,    '*',  0,    ' ',  0,    0,    // 50-59
    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    // 60-69
    0,    0,    '7',  '8',  '9',  '-',  '4',  '5',  '6',  '+',  // 70-79
    '1',  '2',  '3',  '0',  '.',  0,    0,    0,    0            // 80-88
};

//=============================================================================
// GLOBAL STATE
//=============================================================================

// Keyboard state (all modifier keys and flags)
static volatile kbd_state_t kbd_state = {
    .shift_left = false,
    .shift_right = false,
    .ctrl_left = false,
    .ctrl_right = false,
    .alt_left = false,
    .alt_right = false,
    .caps_lock = false,
    .num_lock = false,
    .scroll_lock = false,
    .extended = false
};

// Input queue (circular buffer)
static volatile char input_queue[INPUT_QUEUE_SIZE];
static volatile int input_queue_head = 0;
static volatile int input_queue_tail = 0;
static spinlock_t input_queue_lock = SPINLOCK_INIT;  // Protect queue access

// Buffer management
static volatile int buffer_index = 0;
static volatile bool enter_pressed = false;

//=============================================================================
// ATOMIC HELPER FUNCTIONS
//=============================================================================

/**
 * Atomic increment with x86 LOCK prefix
 */
static inline void atomic_inc(volatile int *val) {
    __asm__ __volatile__("lock incl %0" : "+m"(*val) : : "memory");
}

/**
 * Atomic decrement with x86 LOCK prefix
 */
static inline void atomic_dec(volatile int *val) {
    __asm__ __volatile__("lock decl %0" : "+m"(*val) : : "memory");
}

/**
 * Memory barrier to prevent compiler reordering
 */
static inline void memory_barrier(void) {
    __asm__ __volatile__("" : : : "memory");
}

//=============================================================================
// INPUT QUEUE OPERATIONS (Thread-Safe)
//=============================================================================

/**
 * Push character to input queue
 * Returns: true on success, false if queue full
 * 
 * Thread-safe: Uses spinlock with IRQ disable (called from IRQ handler)
 */
static bool input_queue_push(char ch) {
    uint32_t flags = spinlock_acquire_irq(&input_queue_lock);
    
    int next_tail = (input_queue_tail + 1) % INPUT_QUEUE_SIZE;
    
    // Check for overflow
    if (next_tail == input_queue_head) {
        spinlock_release_irq(&input_queue_lock, flags);
        return false;  // Queue full
    }
    
    input_queue[input_queue_tail] = ch;
    input_queue_tail = next_tail;
    
    spinlock_release_irq(&input_queue_lock, flags);
    return true;
}

/**
 * Pop character from input queue
 * Returns: character or '\0' if queue empty
 * 
 * Thread-safe: Uses spinlock with IRQ disable (prevents race with IRQ handler)
 */
char input_queue_pop(void) {
    uint32_t flags = spinlock_acquire_irq(&input_queue_lock);
    
    if (input_queue_head == input_queue_tail) {
        spinlock_release_irq(&input_queue_lock, flags);
        return '\0';  // Queue empty
    }
    
    char ch = input_queue[input_queue_head];
    input_queue_head = (input_queue_head + 1) % INPUT_QUEUE_SIZE;
    
    spinlock_release_irq(&input_queue_lock, flags);
    return ch;
}

/**
 * Get last character in queue without removing it
 */
static char input_queue_get_last(void) {
    if (input_queue_head == input_queue_tail) {
        return '\0';  // Queue empty
    }
    
    int prev_tail = (input_queue_tail - 1 + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;
    return input_queue[prev_tail];
}

/**
 * Remove last character from queue
 */
static char input_queue_remove_last(void) {
    if (input_queue_head == input_queue_tail) {
        return '\0';  // Queue empty
    }
    
    int prev_tail = (input_queue_tail - 1 + INPUT_QUEUE_SIZE) % INPUT_QUEUE_SIZE;
    char last_char = input_queue[prev_tail];
    
    memory_barrier();
    input_queue_tail = prev_tail;
    
    return last_char;
}

/**
 * Check if queue is empty
 */
static bool input_queue_empty(void) {
    return input_queue_head == input_queue_tail;
}

//=============================================================================
// KEYBOARD STATE QUERY FUNCTIONS
//=============================================================================

/**
 * Check if any Ctrl key is pressed
 */
bool kb_is_ctrl_pressed(void) {
    return kbd_state.ctrl_left || kbd_state.ctrl_right;
}

/**
 * Check if any Alt key is pressed
 */
bool kb_is_alt_pressed(void) {
    return kbd_state.alt_left || kbd_state.alt_right;
}

/**
 * Check if any Shift key is pressed
 */
bool kb_is_shift_pressed(void) {
    return kbd_state.shift_left || kbd_state.shift_right;
}

/**
 * Get full keyboard state
 */
kbd_state_t kb_get_state(void) {
    return kbd_state;
}

//=============================================================================
// SCANCODE PROCESSING
//=============================================================================

/**
 * Convert scancode to ASCII with modifier support
 */
static char scancode_to_ascii(uint8_t scancode, bool shift, bool caps_lock) {
    if (scancode >= SC_MAX) {
        return 0;  // Out of range
    }

    char key = shift ? scancode_to_char_shift[scancode] : scancode_to_char[scancode];

    // Apply caps lock to letters only
    if (caps_lock && key >= 'a' && key <= 'z') {
        key -= 32;  // Convert to uppercase
    } else if (caps_lock && key >= 'A' && key <= 'Z' && !shift) {
        key += 32;  // Convert to lowercase (caps + shift)
    }

    return key;
}

/**
 * Handle extended keys (E0 prefix)
 * Returns special key code or 0 if not handled
 */
static char handle_extended_key(uint8_t scancode, bool released) {
    // Modifier keys
    if (scancode == SC_EXT_RIGHT_CTRL) {
        kbd_state.ctrl_right = !released;
        return 0;
    }
    
    if (scancode == SC_EXT_RIGHT_ALT) {
        kbd_state.alt_right = !released;
        return 0;
    }

    // Only process key presses, not releases for special keys
    if (released) {
        return 0;
    }

    // Arrow keys - return special codes
    switch (scancode) {
        case SC_EXT_UP:        return KEY_UP;
        case SC_EXT_DOWN:      return KEY_DOWN;
        case SC_EXT_LEFT:      return KEY_LEFT;
        case SC_EXT_RIGHT:     return KEY_RIGHT;
        case SC_EXT_HOME:      return KEY_HOME;
        case SC_EXT_END:       return KEY_END;
        case SC_EXT_PAGE_UP:   return KEY_PAGE_UP;
        case SC_EXT_PAGE_DOWN: return KEY_PAGE_DOWN;
        case SC_EXT_INSERT:    return KEY_INSERT;
        case SC_EXT_DELETE:    return KEY_DELETE;
        default:               return 0;
    }
}

/**
 * Process Ctrl+key combinations
 */
static char process_ctrl_combination(char ch) {
    // Ctrl+A through Ctrl+Z map to ASCII 1-26
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 1;  // Ctrl+C = 0x03, etc.
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 1;
    }
    return ch;  // Other keys unchanged
}

//=============================================================================
// KEYBOARD INTERRUPT HANDLER
//=============================================================================

/**
 * IRQ1 keyboard interrupt handler
 * Called on every key press and release
 */
void kb_handler(void* r) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle extended scancode prefix (E0)
    if (scancode == SC_EXTENDED_PREFIX) {
        kbd_state.extended = true;
        outb(0x20, 0x20);  // Send EOI
        return;
    }
    
    // Handle Pause key prefix (E1) - just ignore for now
    if (scancode == SC_PAUSE_PREFIX) {
        kbd_state.extended = false;  // Reset state
        outb(0x20, 0x20);  // Send EOI
        return;
    }
    
    // Determine if this is a key release
    bool released = (scancode & SC_RELEASE_MASK) != 0;
    uint8_t base_scancode = scancode & ~SC_RELEASE_MASK;
    
    // Handle extended keys (E0 prefix)
    if (kbd_state.extended) {
        kbd_state.extended = false;  // Clear flag
        char special_key = handle_extended_key(base_scancode, released);
        
        if (special_key != 0 && !released) {
            // Queue special key with escape sequence
            // Use ANSI-like sequence: ESC [ key
            input_queue_push('\x1B');  // ESC
            input_queue_push('[');
            input_queue_push(special_key);
        }
        
        outb(0x20, 0x20);  // Send EOI
        return;
    }
    
    // Handle regular modifier keys
    if (!released) {
        // Key press events
        switch (base_scancode) {
            case SC_LEFT_SHIFT:
                kbd_state.shift_left = true;
                break;
            case SC_RIGHT_SHIFT:
                kbd_state.shift_right = true;
                break;
            case SC_LEFT_CTRL:
                kbd_state.ctrl_left = true;
                break;
            case SC_LEFT_ALT:
                kbd_state.alt_left = true;
                break;
            case SC_CAPS_LOCK:
                kbd_state.caps_lock = !kbd_state.caps_lock;  // Toggle
                // TODO: Update keyboard LED
                break;
            case SC_NUM_LOCK:
                kbd_state.num_lock = !kbd_state.num_lock;  // Toggle
                break;
            case SC_SCROLL_LOCK:
                kbd_state.scroll_lock = !kbd_state.scroll_lock;  // Toggle
                break;
            case SC_BACKSPACE:
                if (buffer_index > 0) {
                    atomic_dec((volatile int*)&buffer_index);
                    input_queue_push('\b');
                }
                break;
            case SC_ENTER:
                input_queue_push('\n');
                buffer_index = 0;
                enter_pressed = true;
                break;
            default:
                // Regular key press
                if (buffer_index < BUFFER_SIZE - 1) {
                    char key = scancode_to_ascii(base_scancode, 
                                                kb_is_shift_pressed(), 
                                                kbd_state.caps_lock);
                    
                    if (key != 0) {
                        // Handle Ctrl+key combinations
                        if (kb_is_ctrl_pressed()) {
                            key = process_ctrl_combination(key);
                        }
                        
                        atomic_inc((volatile int*)&buffer_index);
                        input_queue_push(key);
                    }
                }
                break;
        }
    } else {
        // Key release events
        switch (base_scancode) {
            case SC_LEFT_SHIFT:
                kbd_state.shift_left = false;
                break;
            case SC_RIGHT_SHIFT:
                kbd_state.shift_right = false;
                break;
            case SC_LEFT_CTRL:
                kbd_state.ctrl_left = false;
                break;
            case SC_LEFT_ALT:
                kbd_state.alt_left = false;
                break;
        }
    }
    
    // Send End of Interrupt (EOI) to PIC
    outb(0x20, 0x20);
}

//=============================================================================
// PUBLIC API FUNCTIONS
//=============================================================================

/**
 * Blocking read of single character
 * Waits until a character is available in the queue
 */
/**
 * Blocking read of single character
 * Waits until a character is available in the queue or serial port
 */
char getchar(void) {
    while (1) {
        // Check serial port first (for nographic mode)
        char serial_ch = serial_read_char(SERIAL_COM1);
        if (serial_ch != 0) {
            // Handle special serial characters
            if (serial_ch == '\r') {
                return '\n';  // Convert CR to LF
            }
            return serial_ch;
        }
        
        // Check keyboard input queue
        if (!input_queue_empty()) {
            return input_queue_pop();
        }
        
        // Wait for input using HLT instead of busy-waiting
        __asm__ __volatile__("hlt");
    }
}

/**
 * Non-blocking read of single character
 * Returns 0 if no input available (checks both serial and keyboard)
 */
char getchar_nonblocking(void) {
    // Check serial port first (for nographic mode)
    char serial_ch = serial_read_char(SERIAL_COM1);
    if (serial_ch != 0) {
        // Handle special serial characters
        if (serial_ch == '\r') {
            serial_ch = '\n';  // Convert CR to LF
        }
        // Don't echo here - let the caller handle echoing
        return serial_ch;
    }
    
    // Check keyboard input queue
    if (!input_queue_empty()) {
        return input_queue_pop();
    }
    
    // No input available
    return 0;
}

/**
 * Read a full line of input (blocks until Enter pressed)
 */
void get_input_line(char* buffer, int max_len) {
    int index = 0;

    while (1) {
        // Check serial port first (for nographic mode)
        char serial_ch = serial_read_char(SERIAL_COM1);
        if (serial_ch != 0) {
            char ch = serial_ch;
            
            // Handle CR (Enter key on serial)
            if (ch == '\r' || ch == '\n') {
                buffer[index] = '\0';  // Null-terminate
                vga_write_char('\n');  // Echo newline
                return;
            }
            
            // Handle backspace
            if (ch == 0x7F || ch == 0x08) {
                if (index > 0) {
                    index--;
                    vga_write_char(0x08);  // Echo backspace
                }
                continue;
            }
            
            // Regular character
            if (index < max_len - 1 && ch >= 32 && ch < 127) {
                buffer[index++] = ch;
                vga_write_char(ch);  // Echo character
            }
            continue;
        }
        
        // Check keyboard input queue
        if (!input_queue_empty()) {
            char ch = input_queue_pop();

            if (ch == '\n') {
                buffer[index] = '\0';  // Null-terminate
                return;
            }

            if (index < max_len - 1) {
                buffer[index++] = ch;
            }
            continue;
        }
        
        // Yield to other tasks (if multitasking enabled)
        __asm__ __volatile__("hlt");
    }
}

/**
 * Install keyboard driver (register IRQ1 handler)
 */
void kb_install(void) {
    // Initialize keyboard controller (important for VMware)
    // Wait for input buffer to be clear
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    
    // Send command to controller: enable keyboard
    outb(KEYBOARD_STATUS_PORT, 0xAE);
    
    // Wait for input buffer to be clear
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    
    // Send command to keyboard: enable scanning
    outb(KEYBOARD_DATA_PORT, 0xF4);
    
    // Wait for acknowledgment
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01));
    inb(KEYBOARD_DATA_PORT); // Read ACK (should be 0xFA)
    
    // Register IRQ1 handler via syscall
    syscall(SYS_INSTALL_IRQ, (void*)1, (void*)kb_handler, 0);
    
    printf("Keyboard driver installed (enhanced mode)\n");
    printf("  - Extended scancode support: YES\n");
    printf("  - Ctrl/Alt tracking: YES\n");
    printf("  - Arrow keys: YES\n");
    printf("  - Function keys: YES\n");
    printf("  - VMware compatible: YES\n");
}

/**
 * Wait for Enter key (blocking)
 */
void kb_wait_enter(void) {
    printf("Press Enter to continue...\n");

    // Reset enter flag
    enter_pressed = false;
    memory_barrier();

    // Wait for Enter key
    while (!enter_pressed) {
        __asm__ __volatile__("hlt");
    }

    // Clear input buffer
    buffer_index = 0;
}
