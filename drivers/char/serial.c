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

// Line Status Register bits
#define SERIAL_LSR_DATA_READY      0x01
#define SERIAL_LSR_TRANSMIT_EMPTY  0x20

//=============================================================================
// INITIALIZATION
//=============================================================================

/**
 * Initialize a serial port
 * @param port Base I/O port address (e.g., SERIAL_COM1)
 */
void serial_init(uint16_t port) {
    outb(SERIAL_INT_ENABLE(port), 0x00);    // Disable interrupts
    outb(SERIAL_LINE_CTRL(port), 0x80);     // Enable DLAB (set baud rate divisor)
    outb(SERIAL_DATA(port), 0x01);          // Set divisor to 1 (115200 baud)
    outb(SERIAL_INT_ENABLE(port), 0x00);    // High byte of divisor
    outb(SERIAL_LINE_CTRL(port), 0x03);     // 8 bits, no parity, one stop bit
    outb(SERIAL_FIFO_CTRL(port), 0xC7);     // Enable FIFO, clear with 14-byte threshold
    outb(SERIAL_MODEM_CTRL(port), 0x0B);    // IRQs enabled, RTS/DSR set
}

/**
 * Initialize COM1 as default serial port (for console)
 */
void serial_init_default(void) {
    serial_init(SERIAL_COM1);
}

//=============================================================================
// STATUS CHECKS
//=============================================================================

/**
 * Check if data is available to read
 * @param port Base I/O port address
 * @return true if data is available
 */
bool serial_received(uint16_t port) {
    return inb(SERIAL_LINE_STATUS(port)) & SERIAL_LSR_DATA_READY;
}

/**
 * Check if transmit buffer is empty
 * @param port Base I/O port address
 * @return true if ready to transmit
 */
bool serial_is_transmit_empty(uint16_t port) {
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
    // Wait for transmit buffer to be empty
    while (!serial_is_transmit_empty(port));
    
    // Send character
    outb(SERIAL_DATA(port), ch);
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

//=============================================================================
// READ OPERATIONS
//=============================================================================

/**
 * Read a character from serial port (non-blocking)
 * @param port Base I/O port address
 * @return Character read, or 0 if no data available
 */
char serial_read_char(uint16_t port) {
    if (serial_received(port)) {
        return inb(SERIAL_DATA(port));
    }
    return 0;
}
