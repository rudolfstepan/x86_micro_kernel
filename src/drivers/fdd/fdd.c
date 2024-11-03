#include "fdd.h"
#include "drivers/io/io.h"
#include "toolchain/stdio.h"


#define TIMEOUT_LIMIT 10000  // Arbitrary timeout limit for waiting loops

// Function to wait until the FDC is ready for commands, with a timeout
static inline bool fdc_wait() {
    int timeout = TIMEOUT_LIMIT;
    while (!(inb(FDD_MSR) & 0x80) && --timeout > 0);  // Wait until FDC is ready for a new command
    return timeout > 0;
}

// Function to send a command to the FDC
static inline bool fdc_send_command(uint8_t command) {
    if (!fdc_wait()) return false;  // Timeout occurred
    outb(FDD_FIFO, command);
    return true;
}

// Function to wait for the motor to spin up (arbitrary delay)
static inline void fdd_motor_on() {
    outb(FDD_DOR, 0x1C);  // 0x1C = motor on, drive select
    for (volatile int i = 0; i < TIMEOUT_LIMIT; i++);  // Delay for motor spin-up
}

// Function to turn off the FDD motor
static inline void fdd_motor_off() {
    outb(FDD_DOR, 0x0C);  // Motor off
}

// Function to recalibrate the FDD (move head to track 0)
static bool fdd_recalibrate() {
    fdd_motor_on();
    if (!fdc_send_command(0x07)) {  // Recalibrate command
        return false;
    }
    // Wait for interrupt (could add handling here, depends on your environment)
    return true;
}

// Reads a sector from the FDD
bool fdd_read_sector(int drive, int head, int track, int sector, uint8_t* buffer){
    if (!buffer) return false;

    // Recalibrate the drive to ensure it's in a known state
    if (!fdd_recalibrate(drive)) {
        printf("FDC recalibration failed.\n");
        return false;
    }

    // Turn on the motor for the specified drive
    fdd_motor_on(drive);

    // Send the READ command to the FDC
    if (!fdc_send_command(FDD_CMD_READ)) return false;
    
    // Send parameters for the read command
    // The (head << 2) | drive combination specifies both the head and drive number
    fdc_send_command((head << 2) | (drive & 0x03)); // Specify drive and head
    fdc_send_command(track);                         // Track number
    fdc_send_command(head);                          // Head number
    fdc_send_command(sector);                        // Sector number (1-based)
    fdc_send_command(2);                             // Bytes per sector (2 = 512 bytes for 1.44MB disks)
    fdc_send_command(18);                            // Last sector number in track (usually 18 for 1.44MB)
    fdc_send_command(0x1B);                          // Gap length (0x1B is standard for 1.44MB)
    fdc_send_command(0xFF);                          // Data length (0xFF when unused)

    // Wait for the FDC to be ready for data transfer
    int timeout = TIMEOUT_LIMIT;
    while (!(inb(FDD_MSR) & 0x80) && --timeout > 0);
    if (timeout <= 0) {
        printf("Timeout waiting for FDC to be ready for data transfer.\n");
        fdd_motor_off(drive);
        return false;
    }

    // Read data from the FDC FIFO into the buffer
    insw(FDD_FIFO, buffer, SECTOR_SIZE / 2);

    // Turn off the motor after the read is complete
    fdd_motor_off(drive);

    return true;
}

// Writes a sector to the FDD
bool fdd_write_sector(int drive, int head, int track, int sector, uint8_t* buffer){
    if (!buffer) return false;

    // Recalibrate the drive to ensure the head is on track 0
    if (!fdd_recalibrate()) {
        printf("FDC recalibration failed.\n");
        return false;
    }

    // Wait for the motor to spin up
    fdd_motor_on();

    // Send the Write Data command with CHS addressing
    if (!fdc_send_command(FDD_CMD_WRITE)) {
        printf("FDC write command failed.\n");
        fdd_motor_off();
        return false;
    }
    
    // The (head << 2) | drive combination specifies both the head and drive number
    fdc_send_command((head << 2) | (drive & 0x03)); // Specify drive and head
    fdc_send_command(track);                         // Track number
    fdc_send_command(head);                          // Head number
    fdc_send_command(sector);                        // Sector number (1-based)
    fdc_send_command(2);                             // Bytes per sector (2 = 512 bytes for 1.44MB disks)
    fdc_send_command(18);                            // Last sector number in track (usually 18 for 1.44MB)
    fdc_send_command(0x1B);                          // Gap length (0x1B is standard for 1.44MB)
    fdc_send_command(0xFF);                          // Data length (0xFF when unused)

    // Wait for the FDC to be ready to accept data, with timeout
    int timeout = TIMEOUT_LIMIT;
    while (!(inb(FDD_MSR) & 0x80) && --timeout > 0);
    if (timeout <= 0) {
        printf("Timeout waiting for FDC to be ready to accept data.\n");
        fdd_motor_off();
        return false;
    }

    // Write the data to the FDD
    outsw(FDD_FIFO, buffer, SECTOR_SIZE / 2);

    // Turn off the motor
    fdd_motor_off();

    return true;
}