#include "serial.h"
#include "io.h"

//=============================================================================
// SERIAL PORT REGISTERS
//=============================================================================

#define SERIAL_DATA(base)          (base)
#define SERIAL_INT_ENABLE(base)    (base + 1)
#define SERIAL_FIFO_CTRL(base)     (base + 2)
#define SERIAL_LINE_CTRL(base)     (base + 3)
#define SERIAL_MODEM_CTRL(base)    (base + 4)
#define SERIAL_LINE_STATUS(base)   (base + 5)
#define SERIAL_SCRATCH(base)       (base + 7)

// Line Status Register bits
#define SERIAL_LSR_TRANSMIT_EMPTY  0x20

#define SERIAL_TX_POLL_LIMIT       100000u
#define SERIAL_PROBE_PATTERN_A     0x5AU
#define SERIAL_PROBE_PATTERN_B     0xA5U

static bool serial_com1_present;

/* The 16550 scratch register has no side effects on line operation. Two
 * distinct readbacks distinguish a decoded UART from an absent ISA range
 * returning a constant all-ones value. Preserve the firmware value. */
static bool serial_probe_port(uint16_t port) {
    uint8_t original = inb(SERIAL_SCRATCH(port));
    outb(SERIAL_SCRATCH(port), SERIAL_PROBE_PATTERN_A);
    uint8_t first = inb(SERIAL_SCRATCH(port));
    outb(SERIAL_SCRATCH(port), SERIAL_PROBE_PATTERN_B);
    uint8_t second = inb(SERIAL_SCRATCH(port));
    outb(SERIAL_SCRATCH(port), original);
    return first == SERIAL_PROBE_PATTERN_A &&
           second == SERIAL_PROBE_PATTERN_B;
}

//=============================================================================
// INITIALIZATION
//=============================================================================

/**
 * Initialize a serial port
 * @param port Base I/O port address (e.g., SERIAL_COM1)
 */
void serial_init(uint16_t port) {
    if (port == SERIAL_COM1 && !serial_com1_present) return;
    outb(SERIAL_INT_ENABLE(port), 0x00);    // Disable interrupts
    outb(SERIAL_LINE_CTRL(port), 0x80);     // Enable DLAB (set baud rate divisor)
    outb(SERIAL_DATA(port), 0x01);          // Set divisor to 1 (115200 baud)
    outb(SERIAL_INT_ENABLE(port), 0x00);    // High byte of divisor
    outb(SERIAL_LINE_CTRL(port), 0x03);     // 8 bits, no parity, one stop bit
    // Clear stale firmware state and enable the FIFOs. Receive data remains
    // ignored because IER and the PIC route stay disabled.
    outb(SERIAL_FIFO_CTRL(port), 0x07);
    // DTR/RTS only. OUT2 remains clear because this console is output-only
    // and must never route a UART interrupt into the PIC.
    outb(SERIAL_MODEM_CTRL(port), 0x03);
}

/**
 * Initialize COM1 as default serial port (for console)
 */
void serial_init_default(void) {
    serial_com1_present = serial_probe_port(SERIAL_COM1);
    if (serial_com1_present) serial_init(SERIAL_COM1);
}

bool serial_default_present(void) {
    return serial_com1_present;
}

/**
 * Check if transmit buffer is empty
 * @param port Base I/O port address
 * @return true if ready to transmit
 */
bool serial_is_transmit_empty(uint16_t port) {
    if (port == SERIAL_COM1 && !serial_com1_present) return true;
    return inb(SERIAL_LINE_STATUS(port)) & SERIAL_LSR_TRANSMIT_EMPTY;
}

//=============================================================================
// WRITE OPERATIONS
//=============================================================================

/**
 * Write a single character to serial port
 * @param port Base I/O port address
 * @param ch Character to write
 */
void serial_write_char(uint16_t port, char ch) {
    if (port == SERIAL_COM1 && !serial_com1_present) return;
    for (uint32_t poll = 0U; poll < SERIAL_TX_POLL_LIMIT; ++poll) {
        if (serial_is_transmit_empty(port)) {
            outb(SERIAL_DATA(port), ch);
            return;
        }
        __asm__ __volatile__("pause");
    }
}

/**
 * Write a null-terminated string to serial port
 * @param port Base I/O port address
 * @param str String to write
 */
void serial_write_string(uint16_t port, const char* str) {
    while (*str) {
        serial_write_char(port, *str++);
    }
}
