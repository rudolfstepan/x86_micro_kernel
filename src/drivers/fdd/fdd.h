#ifndef FDD_H
#define FDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FDD_DOR        0x3F2 // Digital Output Register
#define FDD_MSR        0x3F4 // Main Status Register
#define FDD_FIFO       0x3F5 // Data Register
#define FDD_CTRL       0x3F7 // Control Register

#define FDD_CMD_READ   0xE6  // Read Data command
#define FDD_CMD_WRITE  0xC5  // Write Data command

#define SECTOR_SIZE    512   // Sector size for FDD
#define FDD_SECTOR_CNT 1     // Reading/writing 1 sector at a time


bool fdd_read_sector(unsigned int cylinder, unsigned int head, unsigned int sector, void* buffer);
bool fdd_write_sector(unsigned int cylinder, unsigned int head, unsigned int sector, const void* buffer);


#endif // FDD_H