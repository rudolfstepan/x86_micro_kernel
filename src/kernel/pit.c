// Programmable Interval Timer (PIT) driver
#include "pit.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "toolchain/stdio.h"


#define PIT_FREQUENCY 1193182  // Standard PIT frequency in Hz

// I/O port addresses for the PIT
#define PIT_COMMAND_PORT 0x43  // PIT command port
#define PIT_CHANNEL_0_PORT 0x40 // PIT channel 0 data port
#define PIT_CHANNEL_1_PORT 0x41 // Rarely used
#define PIT_CHANNEL_2_PORT 0x42 // Used for speaker control

// PIT command byte flags (modes and configurations)
#define PIT_MODE_0 0x00 // Interrupt on terminal count
#define PIT_MODE_1 0x02 // Hardware re-triggerable one-shot
#define PIT_MODE_2 0x04 // Rate generator
#define PIT_MODE_3 0x06 // Square wave generator
#define PIT_MODE_4 0x08 // Software triggered strobe
#define PIT_MODE_5 0x0A // Hardware triggered strobe

// Command byte format for PIT (0x36 for channel 0, access mode lobyte/hibyte, mode 3)
#define PIT_CMD_BINARY 0x00      // Use binary mode
#define PIT_CMD_MODE_3 0x06      // Square wave generator mode
#define PIT_CMD_LOHI 0x30        // Access mode: lobyte/hibyte
#define PIT_CMD_CHANNEL_0 0x00   // Channel 0 select

// Combined command byte for common use (channel 0, lobyte/hibyte access, square wave mode)
#define PIT_COMMAND_BYTE (PIT_CMD_CHANNEL_0 | PIT_CMD_LOHI | PIT_CMD_MODE_3 | PIT_CMD_BINARY)

// Maximum value for a 32-bit unsigned counter
#define TIMER_MAX UINT32_MAX  // Maximum value for a 32-bit unsigned counter

static volatile uint32_t timer_tick_count = 0;  // Use a 32-bit counter

void timer_irq_handler(void* r) {
    // Increment a counter each time the timer interrupt fires
    timer_tick_count++;

    // Send End of Interrupt (EOI) signal to the PIC
    outb(0x20, 0x20);  // EOI for master PIC
}

// Function to initialize the PIT with a given frequency
void init_pit(uint32_t frequency) {
    uint16_t divisor = (uint16_t)(PIT_FREQUENCY / frequency);

    // Send command byte: 0x36 = 00 11 011 0 (channel 0, access mode lobyte/hibyte, mode 3)
    outb(PIT_COMMAND_PORT, 0x36);

    // Send the divisor (low byte first, then high byte)
    outb(PIT_CHANNEL_0_PORT, (uint8_t)(divisor & 0xFF));   // Low byte
    outb(PIT_CHANNEL_0_PORT, (uint8_t)((divisor >> 8) & 0xFF)); // High byte
}

void timer_install(uint8_t ms) {
    printf("Install Timer and set an interval for %d ms\n", ms);
    init_pit(1000 / ms); // Set PIT frequency based on the desired millisecond interval
}

// Function to read the current PIT counter
uint16_t read_pit_counter() {
    uint16_t count;

    // Send latch command to PIT (channel 0)
    outb(0x43, 0x00);

    // Read the counter value (low byte first, then high byte)
    count = inb(0x40);       // Read the low byte
    count |= inb(0x40) << 8; // Read the high byte

    return count;
}

void pit_delay(uint32_t milliseconds) {
    //printf("Delay for %d ms\n", milliseconds);

    uint32_t start_tick = timer_tick_count;
    uint32_t ticks_to_wait = milliseconds;
    uint32_t failsafe_timeout = 10000; // 10 seconds
    uint32_t failsafe_start = start_tick;

    while (1) {
        uint32_t current_tick = timer_tick_count;

        // Check if the desired delay has passed
        if ((current_tick - start_tick) >= ticks_to_wait) {
            break;
        }

        // Check if failsafe timeout has been exceeded
        if ((current_tick - failsafe_start) >= failsafe_timeout) {
            printf("Warning: Delay loop timed out\n");
            break;
        }

        // Optionally check PIT value without `printf` to avoid recursion
        // if ((current_tick - start_tick) % 100 == 0) {
        //     uint16_t current_pit_value = read_pit_counter();
        //     // Replace with lightweight debug output, if needed
        //     printf("Updated PIT counter value: %d\n", current_pit_value);
        // }
    }
}
