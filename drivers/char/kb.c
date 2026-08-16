#include "kb.h"
#include "drivers/char/io.h"
#include "drivers/char/serial.h"
#include "drivers/video/video.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/include/interrupt.h"
#include "include/kernel/panic.h"
#include "include/lib/spinlock.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
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
#define PIC1_DATA_PORT          0x21

#define I8042_STATUS_OUTPUT_FULL           0x01U
#define I8042_STATUS_INPUT_FULL            0x02U
#define I8042_STATUS_AUX_DATA              0x20U
#define I8042_STATUS_ERROR_MASK            0xC0U
#define I8042_CMD_READ_CONFIG              0x20U
#define I8042_CMD_WRITE_CONFIG             0x60U
#define I8042_CMD_TEST_CONTROLLER          0xAAU
#define I8042_CMD_DISABLE_PORT1            0xADU
#define I8042_CMD_ENABLE_PORT1             0xAEU
#define I8042_CMD_TEST_PORT1                0xABU
#define I8042_CMD_DISABLE_PORT2            0xA7U
#define I8042_CONTROLLER_TEST_OK           0x55U
#define I8042_PORT_TEST_OK                 0x00U
#define I8042_CONFIG_IRQ1                  0x01U
#define I8042_CONFIG_PORT1_CLOCK_DISABLED  0x10U
#define I8042_CONFIG_TRANSLATION           0x40U
#define I8042_KEYBOARD_SET_SCANCODE        0xF0U
#define I8042_KEYBOARD_SET_LEDS            0xEDU
#define I8042_KEYBOARD_DISABLE_SCANNING    0xF5U
#define I8042_KEYBOARD_ENABLE_SCANNING     0xF4U
#define I8042_KEYBOARD_ACK                 0xFAU
#define I8042_KEYBOARD_RESEND              0xFEU

#define I8042_POLL_LIMIT              100000U
#define I8042_FLUSH_BUDGET            32U
#define I8042_COMMAND_RETRY_LIMIT     2U
#define KEYBOARD_DRAIN_BUDGET         16U
#define KEYBOARD_POLL_INTERVAL_MS     10U
#define KEYBOARD_LED_SCROLL_LOCK      0x01U
#define KEYBOARD_LED_NUM_LOCK         0x02U
#define KEYBOARD_LED_CAPS_LOCK        0x04U
#define KEYBOARD_TRACE_CAPACITY       16U

#define SC_MAX                  89  // Extended to cover more scancodes
#define INPUT_QUEUE_SIZE        256

// Scancode prefixes
#define SC_EXTENDED_PREFIX      0xE0  // Extended keys (arrows, etc.)
#define SC_PAUSE_PREFIX         0xE1  // Pause key (rarely used)
#define SET2_RELEASE_PREFIX     0xF0
#define SET2_PAUSE_TRAILING_BYTES 7U

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

typedef struct {
    uint8_t status;
    uint8_t raw;
    uint8_t mapped;
    uint8_t queued;
} keyboard_trace_entry_t;

// Input queue (circular buffer)
static volatile char input_queue[INPUT_QUEUE_SIZE];
static volatile int input_queue_head = 0;
static volatile int input_queue_tail = 0;
static spinlock_t input_queue_lock = SPINLOCK_INIT;  // Protect queue access
static wait_queue_t input_waiters = WAIT_QUEUE_INIT;
static bool serial_swallow_line_feed = false;
static bool keyboard_irq_registered;
static bool keyboard_scanning_enabled;
static bool set2_release_pending;
static uint8_t set2_pause_bytes_remaining;
static bool keyboard_led_update_pending;
static keyboard_trace_entry_t keyboard_trace[KEYBOARD_TRACE_CAPACITY];
static uint8_t keyboard_trace_head;
static uint8_t keyboard_trace_tail;
static uint8_t keyboard_trace_count;
static uint8_t keyboard_trace_recorded;
static bool keyboard_trace_enabled;

static bool i8042_wait_input_clear(void) {
    for (uint32_t poll = 0U; poll < I8042_POLL_LIMIT; ++poll) {
        if ((inb(KEYBOARD_STATUS_PORT) & I8042_STATUS_INPUT_FULL) == 0U)
            return true;
        __asm__ __volatile__("pause");
    }
    return false;
}

static bool i8042_wait_output_full(uint8_t *status_out) {
    if (status_out == NULL) return false;
    for (uint32_t poll = 0U; poll < I8042_POLL_LIMIT; ++poll) {
        uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if ((status & I8042_STATUS_OUTPUT_FULL) != 0U) {
            *status_out = status;
            return true;
        }
        __asm__ __volatile__("pause");
    }
    return false;
}

static bool i8042_write_command(uint8_t command) {
    if (!i8042_wait_input_clear()) return false;
    outb(KEYBOARD_STATUS_PORT, command);
    return true;
}

static bool i8042_write_data(uint8_t value) {
    if (!i8042_wait_input_clear()) return false;
    outb(KEYBOARD_DATA_PORT, value);
    return true;
}

static bool i8042_read_data(uint8_t *value_out) {
    uint8_t status = 0U;
    if (value_out == NULL || !i8042_wait_output_full(&status)) return false;
    uint8_t value = inb(KEYBOARD_DATA_PORT);
    if ((status & (I8042_STATUS_AUX_DATA | I8042_STATUS_ERROR_MASK)) != 0U)
        return false;
    *value_out = value;
    return true;
}

static void i8042_flush_output(void) {
    for (uint32_t count = 0U; count < I8042_FLUSH_BUDGET; ++count) {
        if ((inb(KEYBOARD_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) == 0U)
            return;
        (void)inb(KEYBOARD_DATA_PORT);
    }
}

static bool i8042_keyboard_command(uint8_t command) {
    for (uint32_t attempt = 0U;
         attempt < I8042_COMMAND_RETRY_LIMIT; ++attempt) {
        uint8_t response = 0U;
        if (!i8042_write_data(command) || !i8042_read_data(&response))
            return false;
        if (response == I8042_KEYBOARD_ACK) return true;
        if (response != I8042_KEYBOARD_RESEND) return false;
    }
    return false;
}

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

/* Convert a raw scan-set-2 make code to the existing Set-1 semantic key
 * numbers. Unknown input has one explicit fail-closed result and cannot index
 * outside fixed storage. */
static uint8_t set2_to_set1(uint8_t scancode) {
    switch (scancode) {
        case 0x01U: return 0x43U; /* F9 */
        case 0x03U: return 0x3FU; /* F5 */
        case 0x04U: return 0x3DU; /* F3 */
        case 0x05U: return 0x3BU; /* F1 */
        case 0x06U: return 0x3CU; /* F2 */
        case 0x07U: return 0x58U; /* F12 */
        case 0x09U: return 0x44U; /* F10 */
        case 0x0AU: return 0x42U; /* F8 */
        case 0x0BU: return 0x40U; /* F6 */
        case 0x0CU: return 0x3EU; /* F4 */
        case 0x0DU: return SC_TAB;
        case 0x0EU: return 0x29U; /* grave */
        case 0x11U: return SC_LEFT_ALT;
        case 0x12U: return SC_LEFT_SHIFT;
        case 0x14U: return SC_LEFT_CTRL;
        case 0x15U: return 0x10U; /* Q */
        case 0x16U: return 0x02U; /* 1 */
        case 0x1AU: return 0x2CU; /* Z */
        case 0x1BU: return 0x1FU; /* S */
        case 0x1CU: return 0x1EU; /* A */
        case 0x1DU: return 0x11U; /* W */
        case 0x1EU: return 0x03U; /* 2 */
        case 0x21U: return 0x2EU; /* C */
        case 0x22U: return 0x2DU; /* X */
        case 0x23U: return 0x20U; /* D */
        case 0x24U: return 0x12U; /* E */
        case 0x25U: return 0x05U; /* 4 */
        case 0x26U: return 0x04U; /* 3 */
        case 0x29U: return 0x39U; /* space */
        case 0x2AU: return 0x2FU; /* V */
        case 0x2BU: return 0x21U; /* F */
        case 0x2CU: return 0x14U; /* T */
        case 0x2DU: return 0x13U; /* R */
        case 0x2EU: return 0x06U; /* 5 */
        case 0x31U: return 0x31U; /* N */
        case 0x32U: return 0x30U; /* B */
        case 0x33U: return 0x23U; /* H */
        case 0x34U: return 0x22U; /* G */
        case 0x35U: return 0x15U; /* Y */
        case 0x36U: return 0x07U; /* 6 */
        case 0x3AU: return 0x32U; /* M */
        case 0x3BU: return 0x24U; /* J */
        case 0x3CU: return 0x16U; /* U */
        case 0x3DU: return 0x08U; /* 7 */
        case 0x3EU: return 0x09U; /* 8 */
        case 0x41U: return 0x33U; /* comma */
        case 0x42U: return 0x25U; /* K */
        case 0x43U: return 0x17U; /* I */
        case 0x44U: return 0x18U; /* O */
        case 0x45U: return 0x0BU; /* 0 */
        case 0x46U: return 0x0AU; /* 9 */
        case 0x49U: return 0x34U; /* period */
        case 0x4AU: return 0x35U; /* slash */
        case 0x4BU: return 0x26U; /* L */
        case 0x4CU: return 0x27U; /* semicolon */
        case 0x4DU: return 0x19U; /* P */
        case 0x4EU: return 0x0CU; /* minus */
        case 0x52U: return 0x28U; /* apostrophe */
        case 0x54U: return 0x1AU; /* left bracket */
        case 0x55U: return 0x0DU; /* equals */
        case 0x58U: return SC_CAPS_LOCK;
        case 0x59U: return SC_RIGHT_SHIFT;
        case 0x5AU: return SC_ENTER;
        case 0x5BU: return 0x1BU; /* right bracket */
        case 0x5DU: return 0x2BU; /* backslash */
        case 0x66U: return SC_BACKSPACE;
        case 0x69U: return SC_EXT_END;
        case 0x6BU: return SC_EXT_LEFT;
        case 0x6CU: return SC_EXT_HOME;
        case 0x70U: return SC_EXT_INSERT;
        case 0x71U: return SC_EXT_DELETE;
        case 0x72U: return SC_EXT_DOWN;
        case 0x73U: return 0x4CU; /* keypad 5 */
        case 0x74U: return SC_EXT_RIGHT;
        case 0x75U: return SC_EXT_UP;
        case 0x76U: return SC_ESCAPE;
        case 0x77U: return SC_NUM_LOCK;
        case 0x78U: return 0x57U; /* F11 */
        case 0x79U: return 0x4EU; /* keypad plus */
        case 0x7AU: return SC_EXT_PAGE_DOWN;
        case 0x7BU: return 0x4AU; /* keypad minus */
        case 0x7CU: return 0x37U; /* keypad multiply */
        case 0x7DU: return SC_EXT_PAGE_UP;
        case 0x7EU: return SC_SCROLL_LOCK;
        case 0x83U: return 0x41U; /* F7 */
        default: return 0U;
    }
}

static void kb_trace_enable_locked(void) {
    KASSERT_IRQ_DISABLED();
    if (keyboard_trace_enabled) return;
    keyboard_trace_head = 0U;
    keyboard_trace_tail = 0U;
    keyboard_trace_count = 0U;
    keyboard_trace_recorded = 0U;
    keyboard_trace_enabled = true;
}

static void kb_trace_record_locked(uint8_t status, uint8_t raw,
                                   uint8_t mapped, bool queued) {
    KASSERT_IRQ_DISABLED();
    if (!keyboard_trace_enabled ||
        keyboard_trace_recorded >= KEYBOARD_TRACE_CAPACITY ||
        keyboard_trace_count >= KEYBOARD_TRACE_CAPACITY) return;

    keyboard_trace_entry_t *entry = &keyboard_trace[keyboard_trace_tail];
    entry->status = status;
    entry->raw = raw;
    entry->mapped = mapped;
    entry->queued = queued ? 1U : 0U;
    keyboard_trace_tail = (uint8_t)(
        (keyboard_trace_tail + 1U) % KEYBOARD_TRACE_CAPACITY);
    ++keyboard_trace_count;
    ++keyboard_trace_recorded;
}

static bool kb_trace_take_locked(keyboard_trace_entry_t *entry_out) {
    KASSERT_IRQ_DISABLED();
    if (entry_out == NULL || keyboard_trace_count == 0U) return false;
    *entry_out = keyboard_trace[keyboard_trace_head];
    keyboard_trace_head = (uint8_t)(
        (keyboard_trace_head + 1U) % KEYBOARD_TRACE_CAPACITY);
    --keyboard_trace_count;
    return true;
}

static void kb_trace_print(const keyboard_trace_entry_t *entry) {
    if (entry == NULL) return;
    printf("[PS2 TRACE st=%02X raw=%02X map=%02X q=%u]\n",
           entry->status, entry->raw, entry->mapped, entry->queued);
}

/* Interpret the non-extended keypad according to NumLock.  Set-2 keypad make
 * codes are first converted to their Set-1 semantic numbers by the mapping
 * above, while E0-prefixed navigation keys bypass this function. */
static bool handle_keypad_key(uint8_t scancode, bool released) {
    char digit = 0;
    char navigation = 0;

    switch (scancode) {
        case 0x37U: digit = '*'; break;
        case 0x4AU: digit = '-'; break;
        case 0x4EU: digit = '+'; break;
        case 0x47U: digit = '7'; navigation = KEY_HOME; break;
        case 0x48U: digit = '8'; navigation = KEY_UP; break;
        case 0x49U: digit = '9'; navigation = KEY_PAGE_UP; break;
        case 0x4BU: digit = '4'; navigation = KEY_LEFT; break;
        case 0x4CU: digit = '5'; break;
        case 0x4DU: digit = '6'; navigation = KEY_RIGHT; break;
        case 0x4FU: digit = '1'; navigation = KEY_END; break;
        case 0x50U: digit = '2'; navigation = KEY_DOWN; break;
        case 0x51U: digit = '3'; navigation = KEY_PAGE_DOWN; break;
        case 0x52U: digit = '0'; navigation = KEY_INSERT; break;
        case 0x53U: digit = '.'; navigation = KEY_DELETE; break;
        default: return false;
    }

    if (released) return true;
    if (scancode == 0x37U || scancode == 0x4AU || scancode == 0x4EU ||
        kbd_state.num_lock) {
        (void)input_queue_push(digit);
    } else if (navigation != 0) {
        queue_extended_key(navigation);
    }
    return true;
}

//=============================================================================
// KEYBOARD INTERRUPT HANDLER
//=============================================================================

static void kb_process_scancode(uint8_t scancode) {
    if (set2_pause_bytes_remaining != 0U) {
        --set2_pause_bytes_remaining;
        return;
    }

    // Handle raw scan-set-2 extended prefix (E0)
    if (scancode == SC_EXTENDED_PREFIX) {
        kbd_state.extended = true;
        return;
    }

    // Pause is the fixed E1 14 77 E1 F0 14 F0 77 sequence.
    if (scancode == SC_PAUSE_PREFIX) {
        kbd_state.extended = false;
        set2_release_pending = false;
        set2_pause_bytes_remaining = SET2_PAUSE_TRAILING_BYTES;
        return;
    }

    if (scancode == SET2_RELEASE_PREFIX) {
        set2_release_pending = true;
        return;
    }

    bool released = set2_release_pending;
    bool extended = kbd_state.extended;
    set2_release_pending = false;
    kbd_state.extended = false;

    uint8_t base_scancode = set2_to_set1(scancode);
    if (base_scancode == 0U) return;

    // Handle extended keys (E0 prefix)
    if (extended) {
        if (base_scancode == SC_ENTER) {
            if (!released) (void)input_queue_push('\n');
            return;
        }
        if (base_scancode == 0x35U) {
            if (!released) (void)input_queue_push('/');
            return;
        }
        char special_key = handle_extended_key(base_scancode, released);
        
        if (special_key != 0 && !released) {
            queue_extended_key(special_key);
        }
        
        return;
    }

    if (handle_keypad_key(base_scancode, released)) return;
    
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
                keyboard_led_update_pending = true;
                break;
            case SC_NUM_LOCK:
                kbd_state.num_lock = !kbd_state.num_lock;  // Toggle
                keyboard_led_update_pending = true;
                break;
            case SC_SCROLL_LOCK:
                kbd_state.scroll_lock = !kbd_state.scroll_lock;  // Toggle
                keyboard_led_update_pending = true;
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

/* Caller keeps IRQs disabled so IRQ1 and the timed polling fallback cannot
 * consume the same output byte.  Mouse/error bytes are discarded rather than
 * being interpreted as raw Set-2 keyboard input. */
static void kb_drain_output_locked(uint32_t budget) {
    for (uint32_t count = 0U; count < budget; ++count) {
        uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if ((status & I8042_STATUS_OUTPUT_FULL) == 0U) return;
        uint8_t scancode = inb(KEYBOARD_DATA_PORT);
        int tail_before = input_queue_tail;
        bool filtered = (status & (I8042_STATUS_AUX_DATA |
                                   I8042_STATUS_ERROR_MASK)) != 0U;
        if (!filtered) kb_process_scancode(scancode);
        kb_trace_record_locked(status, scancode, set2_to_set1(scancode),
                               input_queue_tail != tail_before);
    }
}

/* The decoder may run in IRQ1 and therefore only records a pending LED state.
 * The bounded command/ACK exchange is performed by a console reader in task
 * context with IRQs disabled, so an IRQ handler never waits on the i8042. */
static void kb_service_leds_locked(void) {
    KASSERT_IRQ_DISABLED();
    KASSERT_NOT_IRQ();
    if (!keyboard_led_update_pending || !keyboard_scanning_enabled) return;
    if ((inb(KEYBOARD_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) != 0U) return;

    uint8_t leds = 0U;
    if (kbd_state.scroll_lock) leds |= KEYBOARD_LED_SCROLL_LOCK;
    if (kbd_state.num_lock) leds |= KEYBOARD_LED_NUM_LOCK;
    if (kbd_state.caps_lock) leds |= KEYBOARD_LED_CAPS_LOCK;

    /* Do not retry an ambiguous two-byte transaction automatically: after a
     * failed data ACK the keyboard's command state is not authoritative. */
    keyboard_led_update_pending = false;
    if (!i8042_keyboard_command(I8042_KEYBOARD_SET_LEDS)) return;
    (void)i8042_keyboard_command(leds);
}

static void kb_poll_controller(void) {
    uint32_t flags = irq_save();
    kb_drain_output_locked(KEYBOARD_DRAIN_BUDGET);
    kb_service_leds_locked();
    irq_restore(flags);
}

/**
 * IRQ1 keyboard interrupt handler
 * Called on every key press and release
 */
void kb_handler(void* r) {
    (void)r;
    kb_drain_output_locked(KEYBOARD_DRAIN_BUDGET);
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
    uint32_t setup_flags = irq_save();
    kb_trace_enable_locked();
    irq_restore(setup_flags);

    while (1) {
        uint32_t flags = irq_save();
        kb_drain_output_locked(KEYBOARD_DRAIN_BUDGET);
        kb_service_leds_locked();
        keyboard_trace_entry_t trace;
        bool have_trace = kb_trace_take_locked(&trace);

        // Check serial port first (for nographic mode).
        char ch = read_normalized_serial();
        if (ch == 0) ch = input_queue_pop();
        if (ch != 0 || have_trace) {
            irq_restore(flags);
            if (have_trace) kb_trace_print(&trace);
            if (ch != 0) return ch;
            continue;
        }

        /* The empty check and queue insertion are atomic with both input
         * producers.  A userspace reader no longer occupies the CPU while it
         * waits; kernel-only/preemption-disabled callers retain the safe HLT
         * fallback used during early boot and driver operations. */
        uint64_t now_ms = pit_monotonic_ms();
        uint64_t deadline_ms = UINT64_MAX - now_ms <
                KEYBOARD_POLL_INTERVAL_MS
            ? UINT64_MAX : now_ms + KEYBOARD_POLL_INTERVAL_MS;
        int blocked = wait_queue_block_until_locked(
            &input_waiters, TASK_BLOCK_WAITING, deadline_ms);
        irq_restore(flags);
        if (blocked == 0 || blocked == -110) continue;

        __asm__ __volatile__("hlt");
    }
}

/**
 * Non-blocking read of single character
 * Returns 0 if no input available (checks both serial and keyboard)
 */
char getchar_nonblocking(void) {
    kb_poll_controller();

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
        kb_poll_controller();

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
    // Kernel drivers register their handlers directly; userspace must never
    // be allowed to install arbitrary Ring-0 interrupt callbacks.
    keyboard_irq_registered =
        register_interrupt_handler(1, (void*)kb_handler) == 0;
    keyboard_scanning_enabled = false;
    set2_release_pending = false;
    set2_pause_bytes_remaining = 0U;
    keyboard_led_update_pending = false;
    keyboard_trace_enabled = false;
    keyboard_trace_count = 0U;
    keyboard_trace_recorded = 0U;

    const char *failure_stage = "controller input busy";
    uint8_t response = 0U;
    uint8_t config = 0U;
    bool controller_ready = i8042_wait_input_clear();
    if (!controller_ready) goto install_done;

    failure_stage = "disable first port";
    if (!i8042_write_command(I8042_CMD_DISABLE_PORT1)) goto install_done;
    failure_stage = "disable second port";
    if (!i8042_write_command(I8042_CMD_DISABLE_PORT2)) goto install_done;
    i8042_flush_output();

    failure_stage = "controller self-test";
    if (!i8042_write_command(I8042_CMD_TEST_CONTROLLER) ||
        !i8042_read_data(&response) ||
        response != I8042_CONTROLLER_TEST_OK) goto install_done;

    failure_stage = "first-port self-test";
    if (!i8042_write_command(I8042_CMD_TEST_PORT1) ||
        !i8042_read_data(&response) || response != I8042_PORT_TEST_OK)
        goto install_done;

    failure_stage = "enable first port";
    if (!i8042_write_command(I8042_CMD_ENABLE_PORT1)) goto install_done;

    failure_stage = "read configuration";
    if (!i8042_write_command(I8042_CMD_READ_CONFIG) ||
        !i8042_read_data(&config)) goto install_done;
    config &= (uint8_t)~I8042_CONFIG_PORT1_CLOCK_DISABLED;
    config &= (uint8_t)~I8042_CONFIG_TRANSLATION;
    if (keyboard_irq_registered) config |= I8042_CONFIG_IRQ1;
    else config &= (uint8_t)~I8042_CONFIG_IRQ1;

    failure_stage = "write configuration";
    if (!i8042_write_command(I8042_CMD_WRITE_CONFIG) ||
        !i8042_write_data(config)) goto install_done;

    /* Decode the keyboard's native Set 2 directly. Controller-side
     * translation is not implemented consistently by all physical Super-I/O
     * controllers even when their configuration bit accepts the write. */
    failure_stage = "disable scanning";
    if (!i8042_keyboard_command(I8042_KEYBOARD_DISABLE_SCANNING))
        goto install_done;
    failure_stage = "select scan set";
    if (!i8042_keyboard_command(I8042_KEYBOARD_SET_SCANCODE) ||
        !i8042_keyboard_command(2U)) goto install_done;
    failure_stage = "enable scanning";
    if (!i8042_keyboard_command(I8042_KEYBOARD_ENABLE_SCANNING))
        goto install_done;

    keyboard_scanning_enabled = true;
    if (keyboard_irq_registered) {
        uint8_t mask = inb(PIC1_DATA_PORT);
        outb(PIC1_DATA_PORT, (uint8_t)(mask & (uint8_t)~(1U << 1U)));
    }

install_done:
    if (keyboard_scanning_enabled) {
        printf("PS/2 keyboard ready: config=0x%02X scanset=2-raw input=%s\n",
               config,
               keyboard_irq_registered ? "IRQ1+poll" : "poll-only");
    } else {
        /* Keep the finite poll path active for firmware that left scanning on,
         * but do not claim a verified keyboard without the final F4 ACK. */
        printf("PS/2 keyboard unavailable at %s; input=poll-only\n",
               failure_stage);
    }
}

/**
 * Wait for Enter key (blocking)
 */
void kb_wait_enter(void) {
    printf("Press Enter to continue...\n");
    while (getchar() != '\n') {}
}
