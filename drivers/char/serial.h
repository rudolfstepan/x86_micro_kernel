#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>

// Serial port base addresses
#define SERIAL_COM1 0x3F8
#define SERIAL_COM2 0x2F8
#define SERIAL_COM3 0x3E8
#define SERIAL_COM4 0x2E8

// Initialize serial port
void serial_init(uint16_t port);

// Write operations
void serial_write_char(uint16_t port, char ch);
void serial_write_string(uint16_t port, const char* str);

// Read operations
bool serial_received(uint16_t port);
char serial_read_char(uint16_t port);

// Status checks
bool serial_is_transmit_empty(uint16_t port);

// Helper to initialize COM1 (default)
void serial_init_default(void);

#endif // SERIAL_H
