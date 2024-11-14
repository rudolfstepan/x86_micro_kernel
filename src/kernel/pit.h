#ifndef PIT_H
#define PIT_H

#include <stdint.h>

// PIT base frequency in Hz
#define PIT_FREQUENCY 1193182

// I/O port addresses for the PIT
#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL_0_PORT 0x40
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


extern void timer_irq_handler(void* r);
void timer_install(uint8_t ms);
void pit_delay(unsigned int milliseconds);

#endif // PIT_H