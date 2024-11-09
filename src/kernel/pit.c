// Programmable Interval Timer (PIT) driver
#include "pit.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "toolchain/stdio.h"

volatile int timer_tick_count = 0;

void timer_irq_handler(void* r) {
    // Increment a counter each time the timer interrupt fires
    timer_tick_count++;

    // Optionally, print a message every few ticks
    // if (timer_tick_count % 100 == 0) {
    //     printf("Timer tick: %d\n", timer_tick_count);
    // }

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

void timer_install() {
	printf("Install Timer\n");
	// irq_install_handler(0, timer_irq_handler);
	// printf("done\n");
    init_pit(1000); // 1000 Hz corresponds to 1 ms interval
}
