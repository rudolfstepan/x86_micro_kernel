#include "fdd.h"
#include "drivers/io/io.h"
#include "toolchain/stdio.h"


// Wait until the FDC is ready for a new command
static void fdc_wait() {
    while (!(inb(FDD_MSR) & 0x80));  // Wait until the FDC is ready
}

// Send a command to the FDC via the Data Register (0x3F5)
static bool fdc_send_command(uint8_t command) {
    fdc_wait();  // Wait until FDC is ready
    outb(FDD_FIFO, command);
    return true;
}

// Turn on the FDD motor for a specified drive
static void fdd_motor_on(int drive) {
    outb(FDD_DOR, 0x1C | drive);  // 0x1C = motor on, drive select
    for (volatile int i = 0; i < 10000; i++);  // Small delay for motor spin-up
}

// Turn off the FDD motor
static void fdd_motor_off(int drive) {
    outb(FDD_DOR, drive & 0xFC);  // Motor off
}

// Recalibrate the FDD to reset the head position to track 0
static bool fdd_recalibrate(int drive) {
    fdd_motor_on(drive);  // Ensure motor is on

    // Send recalibrate command (0x07) to FDC
    if (!fdc_send_command(FDD_CMD_RECAL)) {
        printf("Failed to send recalibrate command.\n");
        return false;
    }

    // Send the drive number
    if (!fdc_send_command(drive)) {
        printf("Failed to send drive number for recalibration.\n");
        return false;
    }

    // Wait for recalibration to complete
    while (!(inb(FDD_MSR) & 0x10));  // Wait until recalibration is complete

    fdd_motor_off(drive);
    return true;
}

// Read a sector from the FDD
bool fdd_read_sector(int drive, int head, int track, int sector, void* buffer) {
    if (!buffer) return false;

    // Recalibrate the drive to ensure it’s in a known state
    if (!fdd_recalibrate(drive)) {
        printf("FDC recalibration failed.\n");
        return false;
    }

    // Turn on the motor for the specified drive
    fdd_motor_on(drive);

    // Send the READ command (0xE6) to the FDC
    if (!fdc_send_command(FDD_CMD_READ)) {
        printf("Failed to send READ command.\n");
        fdd_motor_off(drive);
        return false;
    }

    // Send each parameter to the FDC, in the correct order
    if (!fdc_send_command((head << 2) | (drive & 0x03))) {
        printf("Failed to send drive and head.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(track)) {
        printf("Failed to send track number.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(head)) {
        printf("Failed to send head number.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(sector)) {
        printf("Failed to send sector number.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(2)) {  // Sector size (2 = 512 bytes for 1.44MB disks)
        printf("Failed to send sector size.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(18)) {  // Last sector in track
        printf("Failed to send last sector number.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(0x1B)) {  // Gap length (0x1B is standard for 1.44MB)
        printf("Failed to send gap length.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(0xFF)) {  // Data length (unused, set to 0xFF)
        printf("Failed to send data length.\n");
        fdd_motor_off(drive);
        return false;
    }

    // Wait for the FDC to signal that data is ready to be read
    while (!(inb(FDD_MSR) & 0x80));  // Wait until FDC is ready for data transfer

    // Read the data from the FDC FIFO into the buffer
    insw(FDD_FIFO, buffer, SECTOR_SIZE / 2);  // SECTOR_SIZE / 2 for 16-bit transfers

    fdd_motor_off(drive);  // Turn off the motor after the read is complete
    return true;
}

// Writes a sector to the FDD
bool fdd_write_sector(int drive, int head, int track, int sector, void* buffer){
    if (!buffer) return false;

    // Recalibrate the drive to ensure the head is on track 0
    if (!fdd_recalibrate(drive)) {
        printf("FDC recalibration failed.\n");
        return false;
    }

    // Wait for the motor to spin up
    fdd_motor_on(drive);

    // Send the Write Data command with CHS addressing
    if (!fdc_send_command(FDD_CMD_WRITE)) {
        printf("FDC write command failed.\n");
        fdd_motor_off(drive);
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
        fdd_motor_off(drive);
        return false;
    }

    // Write the data to the FDD
    outsw(FDD_FIFO, buffer, SECTOR_SIZE / 2);

    // Turn off the motor
    fdd_motor_off(drive);

    return true;
}

void fdd_detect_drives() {

    // Turn on the motor for FDD 0 (assumes a single FDD attached)
    outb(FDD_DOR, 0x1C);  // 0x1C = motor on, select drive 0

    // Wait briefly for the motor to stabilize
    for (volatile int i = 0; i < 10000; i++);

    // Check the FDD status to confirm if an FDD is present
    if (inb(FDD_MSR) & 0x80) {  // Bit 7 set if FDC is ready

        // Initialize the drive structure
        drive_t* drive = (drive_t*)malloc(sizeof(drive_t));

        drive->type = DRIVE_TYPE_FDD;
        snprintf(drive->name, sizeof(drive->name), "fdd%d", 1);
        drive->cylinder = 80;  // Common for 1.44MB floppies
        drive->head = 2;
        drive->sector = 18;
        printf("Floppy drive detected: %s\n", drive->name);

        // add the drive to the global array of detected drives
        detected_drives[drive_count++] = *drive;
    }

    // Turn off the motor
    outb(FDD_DOR, 0x0C);
}