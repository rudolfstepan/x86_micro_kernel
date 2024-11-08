#ifndef FDD_H
#define FDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/drives.h"

#define FDD_DOR        0x3F2  // Digital Output Register
#define FDD_MSR        0x3F4  // Main Status Register
#define FDD_FIFO       0x3F5  // Data Register
#define FDD_CTRL       0x3F7  // Control Register

#define FDD_CMD_READ   0xE6   // Read Data command
#define FDD_CMD_WRITE  0xC5  // Write Data command
#define FDD_CMD_RECAL  0x07   // Recalibrate command
#define SECTOR_SIZE    512    // Sector size for FDD
#define TIMEOUT_LIMIT  10000  // Arbitrary timeout limit for waiting loops

#define SECTOR_SIZE    512    // Sector size for FDD
#define FDD_SECTOR_CNT 1     // Reading/writing 1 sector at a time

extern void fdd_irq_handler(uint8_t* r);

void fdd_detect_drives();
void debug_read_bootsector(uint8_t sector);
bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer);
bool fdd_write_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer);

#endif // FDD_H