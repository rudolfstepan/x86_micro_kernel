// Programmable Interval Timer (PIT) driver
#include "pit.h"
#include "drivers/io/io.h"
#include "kernel/sys.h"
#include "toolchain/stdio.h"

volatile int timer_ticks = 0;

void timer_irq_handler(void* r) {
    // Increment a counter each time the timer interrupt fires
    timer_ticks++;

    // Optionally, print a message every few ticks
    if (timer_ticks % 100 == 0) {
        printf("Timer tick: %d\n", timer_ticks);
    }

    // Send End of Interrupt (EOI) signal to the PIC
    outb(0x20, 0x20);  // EOI for master PIC
}

void timer_install() {
	printf("Install Timer...");
	// irq_install_handler(0, timer_irq_handler);
	// printf("done\n");
}
