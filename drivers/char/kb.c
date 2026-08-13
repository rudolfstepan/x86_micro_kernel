#include "kb.h"
#include "drivers/char/io.h"
#include "drivers/char/serial.h"
#include "drivers/video/video.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"
#include "include/lib/spinlock.h"
#include "kernel/sched/scheduler.h"
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
static wait_queue_t input_waiters = WAIT_QUEUE_INIT;
static bool serial_swallow_line_feed = false;

static char normalize_serial_input(char ch) {
    if (ch == '\r') {
        serial_swallow_line_feed = true;
        return '\n';
    }
    if (ch == '\n' && serial_swallow_line_feed) {
        serial_swallow_line_feed = false;
        return 0;
    }
    serial_swallow_line_feed = false;
    return ch == 0x7F ? '\b' : ch;
}

/* A swallowed LF from a CRLF pair is not the same as an empty UART.  Keep
 * draining so a following byte already buffered by the IRQ handler is not
 * delayed behind network polling or HLT. */
static char read_normalized_serial(void) {
    char raw;
    while ((raw = serial_read_char(SERIAL_COM1)) != 0) {
        char normalized = normalize_serial_input(raw);
        if (normalized != 0) return normalized;
    }
    return 0;
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
static bool input_queue_push_sequence(const char* sequence, size_t length) {
    if (!sequence || length == 0 || length >= INPUT_QUEUE_SIZE) return false;
    uint32_t flags = spinlock_acquire_irq(&input_queue_lock);

    int next_tail = input_queue_tail;
    for (size_t i = 0; i < length; i++) {
        next_tail = (next_tail + 1) % INPUT_QUEUE_SIZE;
        if (next_tail == input_queue_head) {
            spinlock_release_irq(&input_queue_lock, flags);
            return false;
        }
    }

    for (size_t i = 0; i < length; i++) {
        input_queue[input_queue_tail] = sequence[i];
        input_queue_tail = (input_queue_tail + 1) % INPUT_QUEUE_SIZE;
    }
    /* Publish the bytes and wake a reader under the same IRQ-disabled
     * transaction.  This closes the condition-check/block lost-wakeup window
     * for both PS/2 and serial console input. */
    spinlock_release(&input_queue_lock);
    /* Readiness is level-triggered: a sequence can satisfy more than one
     * blocked reader.  Wake all and let each reader recheck/consume under the
     * atomic condition-loop contract. */
    (void)wait_queue_wake_all_locked(&input_waiters);
    irq_restore(flags);
    return true;
}

static bool input_queue_push(char ch) {
    return input_queue_push_sequence(&ch, 1);
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
 * Check if queue is empty
 */
static bool input_queue_empty(void) {
    return input_queue_head == input_queue_tail;
}

void kb_notify_input_ready(void) {
    uint32_t flags = irq_save();
    (void)wait_queue_wake_all_locked(&input_waiters);
    irq_restore(flags);
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
    } else if (caps_lock && key >= 'A' && key <= 'Z') {
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

static void queue_extended_key(char key);

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
        return;
    }
    
    // Handle Pause key prefix (E1) - just ignore for now
    if (scancode == SC_PAUSE_PREFIX) {
        kbd_state.extended = false;  // Reset state
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
            queue_extended_key(special_key);
        }
        
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
            case SC_ESCAPE:
                (void)input_queue_push('\x1B');
                break;
            case SC_BACKSPACE:
                (void)input_queue_push('\b');
                break;
            case SC_ENTER:
                (void)input_queue_push('\n');
                break;
            default:
                // Regular key press
                {
                    char key = scancode_to_ascii(base_scancode, 
                                                kb_is_shift_pressed(), 
                                                kbd_state.caps_lock);
                    
                    if (key != 0) {
                        // Handle Ctrl+key combinations
                        if (kb_is_ctrl_pressed()) {
                            key = process_ctrl_combination(key);
                        }
                        
                        (void)input_queue_push(key);
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
    
}

/* Queue standard ANSI sequences so PS/2 and serial terminals share one
 * unambiguous decoder (raw Set-1 codes collide with ANSI Home/End). */
static void queue_extended_key(char key) {
    char sequence[4] = {'\x1B', '[', 0, 0};
    size_t length = 3;

    switch (key) {
        case KEY_UP: sequence[2] = 'A'; break;
        case KEY_DOWN: sequence[2] = 'B'; break;
        case KEY_RIGHT: sequence[2] = 'C'; break;
        case KEY_LEFT: sequence[2] = 'D'; break;
        case KEY_HOME: sequence[2] = 'H'; break;
        case KEY_END: sequence[2] = 'F'; break;
        case KEY_INSERT:
            sequence[2] = '2'; sequence[3] = '~'; length = 4; break;
        case KEY_DELETE:
            sequence[2] = '3'; sequence[3] = '~'; length = 4; break;
        case KEY_PAGE_UP:
            sequence[2] = '5'; sequence[3] = '~'; length = 4; break;
        case KEY_PAGE_DOWN:
            sequence[2] = '6'; sequence[3] = '~'; length = 4; break;
        default: return;
    }
    (void)input_queue_push_sequence(sequence, length);
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
    /* Blocking console input must preserve, not silently change, the caller's
     * interrupt contract. */
    KASSERT_CAN_SLEEP();
    while (1) {
        uint32_t flags = irq_save();

        // Check serial port first (for nographic mode).
        char ch = read_normalized_serial();
        if (ch == 0) ch = input_queue_pop();
        if (ch != 0) {
            irq_restore(flags);
            return ch;
        }

        /* The empty check and queue insertion are atomic with both input
         * producers.  A userspace reader no longer occupies the CPU while it
         * waits; kernel-only/preemption-disabled callers retain the safe HLT
         * fallback used during early boot and driver operations. */
        int blocked = wait_queue_block_locked(&input_waiters,
                                              TASK_BLOCK_WAITING);
        irq_restore(flags);
        if (blocked == 0) continue;

        __asm__ __volatile__("hlt");
    }
}

/**
 * Non-blocking read of single character
 * Returns 0 if no input available (checks both serial and keyboard)
 */
char getchar_nonblocking(void) {
    // Check serial port first (for nographic mode)
    char serial_ch = read_normalized_serial();
    if (serial_ch != 0) return serial_ch;
    
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
        char ch = read_normalized_serial();
        if (ch != 0) {
            
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

            if (ch == '\b') {
                if (index > 0) {
                    index--;
                    vga_write_char('\b');
                }
                continue;
            }

            // get_input_line has no cursor editor; consume special-key ANSI
            // sequences instead of returning them as program input.
            if (ch == '\x1B') {
                if (!input_queue_empty() && input_queue_pop() == '[' &&
                    !input_queue_empty()) {
                    char code = input_queue_pop();
                    if (code >= '0' && code <= '9' &&
                        !input_queue_empty()) {
                        (void)input_queue_pop();
                    }
                }
                continue;
            }

            if (index < max_len - 1 && ch >= 32 && ch < 127) {
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
    
    // Kernel drivers register their handlers directly; userspace must never
    // be allowed to install arbitrary Ring-0 interrupt callbacks.
    if (register_interrupt_handler(1, (void*)kb_handler) != 0) {
        printf("Keyboard IRQ registration failed\n");
        return;
    }
    
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
    while (getchar() != '\n') {}
}
