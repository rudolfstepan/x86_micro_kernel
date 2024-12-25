#include "mouse.h"

#include <stdint.h>
#include <stdbool.h>

#include "../io/io.h"
#include "toolchain/stdio.h"
#include "../video/framebuffer.h"

// PS/2 Maus Ports
#define PS2_CMD_PORT  0x64
#define PS2_DATA_PORT 0x60

// Globale Variablen für den Mauscursor
int cursor_x = SCREEN_WIDTH / 2;
int cursor_y = SCREEN_HEIGHT / 2;

// function prototypes
void update_screen_cursor(int x, int y);

void ps2_send_command(uint8_t command) {
    while ((inb(PS2_CMD_PORT) & 0x02) != 0); // Wait until the controller is ready
    outb(PS2_CMD_PORT, command);            // Send command to the command port
}

uint8_t ps2_read_data() {
    while ((inb(PS2_CMD_PORT) & 0x01) == 0); // Wait until data is available
    return inb(PS2_DATA_PORT);
}

void ps2_mouse_init() {
    printf("Initializing PS/2 Mouse\n");

    ps2_send_command(0xA8); // Enable second PS/2 port
    ps2_send_command(0x20); // Request status byte
    uint8_t status = ps2_read_data();
    status |= 0x02;         // Enable IRQ12 for the mouse
    ps2_send_command(0x60); // Write status byte
    outb(PS2_DATA_PORT, status);

    // Initialize the mouse
    ps2_send_command(0xD4); // Send to mouse
    outb(PS2_DATA_PORT, 0xF6); // Set defaults
    if (ps2_read_data() != 0xFA) {
        printf("Mouse did not acknowledge defaults!\n");
    }

    ps2_send_command(0xD4);
    outb(PS2_DATA_PORT, 0xF4); // Enable data reporting
    if (ps2_read_data() != 0xFA) {
        printf("Mouse did not acknowledge enable data reporting!\n");
    }
}

// Funktion, um den Cursor auf dem Bildschirm darzustellen
void update_screen_cursor(int x, int y) {
    // Einfache Implementierung: Zeichne ein Zeichen an der neuen Position
    // (Erfordert eine framebufferähnliche Implementierung)
    static int old_x = 0, old_y = 0;

    // Lösche den alten Cursor
    draw_char(old_x, old_y, ' ', 0x000000);

    // Zeichne den neuen Cursor
    draw_char(x, y, 'X', 0xFFFFFF);

    old_x = x;
    old_y = y;
}

void handle_mouse_packet(uint8_t packet[3]) {
    int8_t x_move = (int8_t)packet[1];
    int8_t y_move = (int8_t)packet[2];

    cursor_x = cursor_x + x_move;
    cursor_y = cursor_y - y_move; // Y-axis is inverted

    cursor_x = cursor_x < 0 ? 0 : (cursor_x >= SCREEN_WIDTH ? SCREEN_WIDTH - 1 : cursor_x);
    cursor_y = cursor_y < 0 ? 0 : (cursor_y >= SCREEN_HEIGHT ? SCREEN_HEIGHT - 1 : cursor_y);

    update_screen_cursor(cursor_x, cursor_y);
}

void ps2_mouse_interrupt() {
    static uint8_t packet[3];
    static uint8_t packet_index = 0;

    packet[packet_index++] = ps2_read_data();

    if (packet_index == 3) {
        handle_mouse_packet(packet);
        packet_index = 0;
    }

    // Send EOI to PICs
    outb(0x20, 0x20); // Master PIC
    outb(0xA0, 0x20); // Slave PIC
}
