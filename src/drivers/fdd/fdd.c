#include "fdd.h"
#include "drivers/io/io.h"

#define FDD_DOR        0x3F2 // Digital Output Register
#define FDD_MSR        0x3F4 // Main Status Register
#define FDD_FIFO       0x3F5 // Data Register
#define FDD_CTRL       0x3F7 // Control Register

#define FDD_CMD_READ   0xE6  // Read Data command
#define FDD_CMD_WRITE  0xC5  // Write Data command

#define SECTOR_SIZE    512   // Sector size for FDD
#define FDD_SECTOR_CNT 1     // Reading/writing 1 sector at a time

// Function to wait until the FDC is ready for commands
static inline void fdc_wait() {
    while (!(inb(FDD_MSR) & 0x80)); // Wait until FDC is ready for a new command
}

// Function to send a command to the FDC
static inline void fdc_send_command(uint8_t command) {
    fdc_wait();
    outb(FDD_FIFO, command);
}

// Reads a sector from the FDD
bool fdd_read_sector(unsigned int cylinder, unsigned int head, unsigned int sector, void* buffer) {
    if (!buffer) return false;

    // Specify the Digital Output Register to select drive and turn on the motor
    outb(FDD_DOR, 0x1C);  // 0x1C = motor on, drive select

    // Send the Read Data command with CHS addressing
    fdc_send_command(FDD_CMD_READ);
    fdc_send_command(head << 2);                // Head and drive number
    fdc_send_command(cylinder);                 // Cylinder
    fdc_send_command(head);                     // Head
    fdc_send_command(sector);                   // Sector
    fdc_send_command(2);                        // Sector size code (2 for 512 bytes)
    fdc_send_command(18);                       // Last sector in track
    fdc_send_command(0x1B);                     // Gap length
    fdc_send_command(0xFF);                     // Data length (0xFF for default)

    // Wait for the FDC to be ready for data transfer
    while (!(inb(FDD_MSR) & 0x80)) {}

    // Read the data from the FDD
    insw(FDD_FIFO, buffer, SECTOR_SIZE / 2);

    // Reset Digital Output Register to stop motor
    outb(FDD_DOR, 0x0C);

    return true;
}

// Writes a sector to the FDD
bool fdd_write_sector(unsigned int cylinder, unsigned int head, unsigned int sector, const void* buffer) {
    if (!buffer) return false;

    // Specify the Digital Output Register to select drive and turn on the motor
    outb(FDD_DOR, 0x1C);  // 0x1C = motor on, drive select

    // Send the Write Data command with CHS addressing
    fdc_send_command(FDD_CMD_WRITE);
    fdc_send_command(head << 2);                // Head and drive number
    fdc_send_command(cylinder);                 // Cylinder
    fdc_send_command(head);                     // Head
    fdc_send_command(sector);                   // Sector
    fdc_send_command(2);                        // Sector size code (2 for 512 bytes)
    fdc_send_command(18);                       // Last sector in track
    fdc_send_command(0x1B);                     // Gap length
    fdc_send_command(0xFF);                     // Data length (0xFF for default)

    // Wait for the FDC to be ready to accept data
    while (!(inb(FDD_MSR) & 0x80)) {}

    // Write the data to the FDD
    outsw(FDD_FIFO, buffer, SECTOR_SIZE / 2);

    // Reset Digital Output Register to stop motor
    outb(FDD_DOR, 0x0C);

    return true;
}
