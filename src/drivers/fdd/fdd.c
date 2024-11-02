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
bool fdd_read_sector(unsigned int cylinder, unsigned int head, unsigned int sector, void* buffer) {
    if (!buffer) return false;

    if (!fdd_recalibrate()) {
        printf("FDC recalibration failed.\n");
        return false;
    }

    fdd_motor_on();

    if (!fdc_send_command(FDD_CMD_READ)) return false;
    fdc_send_command((head << 2) | 0);          
    fdc_send_command(cylinder);                 
    fdc_send_command(head);                     
    fdc_send_command(sector);                   
    fdc_send_command(2);                        
    fdc_send_command(18);                       
    fdc_send_command(0x1B);                     
    fdc_send_command(0xFF);                     

    int timeout = TIMEOUT_LIMIT;
    while (!(inb(FDD_MSR) & 0x80) && --timeout > 0);
    if (timeout <= 0) {
        printf("Timeout waiting for FDC to be ready for data transfer.\n");
        fdd_motor_off();
        return false;
    }

    insw(FDD_FIFO, buffer, SECTOR_SIZE / 2);

    fdd_motor_off();

    return true;
}

// Writes a sector to the FDD
bool fdd_write_sector(unsigned int cylinder, unsigned int head, unsigned int sector, const void* buffer) {
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
    
    fdc_send_command((head << 2) | 0);          // Head and drive number (0 for first drive)
    fdc_send_command(cylinder);                 // Cylinder
    fdc_send_command(head);                     // Head
    fdc_send_command(sector);                   // Sector
    fdc_send_command(2);                        // Sector size code (2 for 512 bytes)
    fdc_send_command(18);                       // Last sector in track
    fdc_send_command(0x1B);                     // Gap length
    fdc_send_command(0xFF);                     // Data length (0xFF for default)

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