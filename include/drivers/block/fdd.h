#ifndef FDD_H
#define FDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/bus/drives.h"

#define FDC_BASE       0x3F0  // Base I/O port for the floppy disk controller
#define FDD_DOR        0x3F2  // Digital Output Register
#define FDD_MSR        0x3F4  // Main Status Register
#define FDD_FIFO       0x3F5  // Data Register
#define FDD_CTRL       0x3F7  // Control Register

#define FDD_CMD_READ   0xE6   // Read Data command
#define FDD_CMD_WRITE  0xC5  // Write Data command
#define FDC_RECALIBRATE_CMD  0x07   // Recalibrate command
#define FDC_SENSE_INTERRUPT_CMD 0x08  // Command to sense interrupt status

#define SECTOR_SIZE    512    // Sector size for FDD
#define TIMEOUT_LIMIT  10000  // Arbitrary timeout limit for waiting loops

#define SECTOR_SIZE    512    // Sector size for FDD
#define FDD_SECTOR_CNT 1     // Reading/writing 1 sector at a time

extern void fdd_irq_handler(uint8_t* r);

void fdc_initialize();
void fdd_detect_drives();
void debug_read_bootsector(uint8_t sector);
bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer);
bool fdc_read_sectors(uint8_t drive, uint8_t head, uint8_t track, uint8_t start_sector, uint8_t num_sectors, void* buffer);
bool fdd_write_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer);

void fdc_motor_on(int drive);
void fdc_motor_off(int drive);

#endif // FDD_H