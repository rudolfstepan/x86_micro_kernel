// Programmable Interval Timer (PIT) driver
#include "pit.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "toolchain/stdio.h"
#include "kernel/system.h"

#define PIT_FREQUENCY 1193182  // Standard PIT frequency in Hz
#define PIT_COMMAND_PORT 0x43  // PIT command port
#define PIT_CHANNEL_0_PORT 0x40 // PIT channel 0 data port

#define TIMER_MAX UINT32_MAX  // Maximum value for a 32-bit unsigned counter

volatile uint32_t timer_tick_count = 0;  // Use a 32-bit counter

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

void pit_delay(unsigned int milliseconds) {
    // Enter a critical section to safely read `timer_tick_count`
    disable_interrupts();
    uint32_t start_tick = timer_tick_count;
    enable_interrupts();

    // Calculate the number of ticks to wait
    uint32_t ticks_to_wait = milliseconds;

    // Wait until the required number of ticks has passed
    while (1) {
        disable_interrupts();  // Enter critical section
        uint32_t current_tick = timer_tick_count;
        enable_interrupts();   // Exit critical section

        if ((current_tick - start_tick) >= ticks_to_wait) {
            break;
        }
    }
}