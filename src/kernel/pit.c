
// #include "pit.h"
#include "io.h"
#include "sys.h"
#include "../toolchain/stdio.h"

void timer_phase(int hertz) {
	int div = 1193180 / hertz;
	outb(0x43, 0x36); //square wave, lsb first and then msb
	outb(0x40, div & 0xFF);
	outb(0x40, div >> 8);
}

unsigned char ab = 48;
unsigned char ab1 = 48;
unsigned long timer_ticks = 0;
unsigned long seconds = 0;

void timer_handler(struct regs* r) {
	(void)r; // Suppress unused parameter warning

	timer_ticks++;
	if (timer_ticks % 18 == 0) {
		seconds++;
		ab++;
		timer_ticks = 0;
	}
}

void timer_install() {
	printf("Install Timer...");
	//timer_phase(500);
	irq_install_handler(0, timer_handler);
	printf("done\n");
}

void delay(int ticks) {
	ab = 0;

	while (ab != ticks) {
		__asm__ __volatile__("nop");
	}
}
