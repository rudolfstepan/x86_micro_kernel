// Programmable Interval Timer (PIT) driver
#include "kernel/time/pit.h"
#include "drivers/char/io.h"
#include "arch/x86/include/sys.h"
#include "arch/x86/include/interrupt.h"
#include "kernel/sched/scheduler.h"
#include "include/kernel/watchdog.h"
#include "include/kernel/supervisor.h"
#include "lib/libc/stdio.h"


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

static volatile uint64_t timer_tick_count;
static uint32_t pit_divisor = PIT_FREQUENCY / 1000U;
static uint32_t pit_millisecond_fraction;

uint32_t pit_ticks(void) {
    return (uint32_t)pit_monotonic_ms();
}

uint64_t pit_monotonic_ms(void) {
    /* A 64-bit load is not atomic on i386.  IRQ0 is the sole writer, so
     * excluding it gives readers a coherent value without a global lock. */
    uint32_t flags = irq_save();
    uint64_t ticks = timer_tick_count;
    irq_restore(flags);
    return ticks;
}

void timer_irq_handler(void* r) {
    /* Accumulate the real programmed PIT interval instead of assuming that
     * the rounded integer divisor is exactly 1 kHz.  With divisor 1193 this
     * removes the former ~13-second-per-day drift. */
    pit_millisecond_fraction += pit_divisor * 1000U;
    timer_tick_count += pit_millisecond_fraction / PIT_FREQUENCY;
    pit_millisecond_fraction %= PIT_FREQUENCY;
    scheduler_wake_expired_sleepers_locked(timer_tick_count);
    scheduler_wake_expired_waiters_locked(timer_tick_count);
    watchdog_clock_tick(timer_tick_count);
    supervisor_clock_tick(timer_tick_count);
}

// Function to initialize the PIT with a given frequency
void init_pit(uint32_t frequency) {
    if (frequency == 0) frequency = 1;
    uint32_t divisor = PIT_FREQUENCY / frequency;
    if (divisor == 0) divisor = 1;
    if (divisor > UINT16_MAX) divisor = UINT16_MAX;
    pit_divisor = divisor;
    pit_millisecond_fraction = 0;

    // Send command byte: 0x36 = 00 11 011 0 (channel 0, access mode lobyte/hibyte, mode 3)
    outb(PIT_COMMAND_PORT, 0x36);

    // Send the divisor (low byte first, then high byte)
    outb(PIT_CHANNEL_0_PORT, (uint8_t)(divisor & 0xFF));   // Low byte
    outb(PIT_CHANNEL_0_PORT, (uint8_t)((divisor >> 8) & 0xFF)); // High byte
}

void timer_install(uint8_t ms) {
    // Install the IRQ handler for the timer
    register_interrupt_handler(0, (void*)timer_irq_handler);
    //printf("Install Timer and set an interval for %d ms\n", ms);
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
    if (milliseconds == 0) return;
    uint64_t start = pit_monotonic_ms();
    while (pit_monotonic_ms() - start < (uint64_t)milliseconds) {
        __asm__ __volatile__("pause");
    }
}
