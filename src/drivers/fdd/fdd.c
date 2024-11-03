#include "fdd.h"
#include "drivers/io/io.h"
#include "toolchain/stdio.h"


#define TIMEOUT_LIMIT 10000  // Arbitrary timeout limit for waiting loops

// Function to wait until the FDC is ready for commands, with a timeout
static inline bool fdc_wait() {
    int timeout = TIMEOUT_LIMIT;
    while (!(inb(FDD_MSR) & 0x80) && --timeout > 0);
    if (timeout <= 0) {
        printf("FDC wait timed out.\n");  // Debug message
        return false;
    }
    return true;
}

// Function to send a command to the FDC
static inline bool fdc_send_command(uint8_t command) {
    if (!fdc_wait()) {
        printf("Failed to send command: 0x%02X\n", command);  // Debug message
        return false;
    }
    outb(FDD_FIFO, command);
    return true;
}

// Function to wait for the motor to spin up (arbitrary delay)
static inline void fdd_motor_on(int drive) {
    uint8_t motor_cmd = 0x10 | (1 << drive);  // Enable motor for specific drive
    outb(FDD_DOR, motor_cmd);
    for (volatile int i = 0; i < TIMEOUT_LIMIT; i++);  // Delay for motor spin-up
}

// Function to turn off the FDD motor
static inline void fdd_motor_off(int drive) {
    uint8_t motor_cmd = 0x0C & ~(1 << drive);  // Disable motor for specific drive
    outb(FDD_DOR, motor_cmd);
}

// Function to recalibrate the FDD (move head to track 0)
static bool fdd_recalibrate(int drive) {
    fdd_motor_on(drive);
    if (!fdc_send_command(0x07)) {  // Recalibrate command
        printf("Failed to send recalibrate command.\n");
        return false;
    }

    // TODO: Wait for interrupt or confirmation (platform-dependent)
    // If your environment has a way to check for interrupt, add it here.
    
    return true;
}

// Reads a sector from the FDD
bool fdd_read_sector(int drive, int head, int track, int sector, uint8_t* buffer) {
    if (!buffer) {
        printf("Buffer is NULL.\n");
        return false;
    }

    // Recalibrate the drive to ensure it's in a known state
    if (!fdd_recalibrate(drive)) {
        printf("FDC recalibration failed.\n");
        return false;
    }

    // Turn on the motor for the specified drive
    fdd_motor_on(drive);

    // Send the READ command to the FDC
    if (!fdc_send_command(FDD_CMD_READ)) {
        printf("Failed to send READ command.\n");
        fdd_motor_off(drive);
        return false;
    }
    
    // Send parameters for the read command
    if (!fdc_send_command((head << 2) | (drive & 0x03))) {
        printf("Failed to send drive/head.\n");
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
    if (!fdc_send_command(2)) {  // 512 bytes per sector
        printf("Failed to send sector size.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(18)) {  // Last sector number in track
        printf("Failed to send last sector number.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(0x1B)) {  // Gap length
        printf("Failed to send gap length.\n");
        fdd_motor_off(drive);
        return false;
    }
    if (!fdc_send_command(0xFF)) {  // Data length (0xFF when unused)
        printf("Failed to send data length.\n");
        fdd_motor_off(drive);
        return false;
    }

    // Wait for the FDC to be ready for data transfer
    int timeout = TIMEOUT_LIMIT;
    while (!(inb(FDD_MSR) & 0x80) && --timeout > 0);
    if (timeout <= 0) {
        printf("Timeout waiting for FDC to be ready for data transfer.\n");
        fdd_motor_off(drive);
        return false;
    }

    // Read data from the FDC FIFO into the buffer
    insw(FDD_FIFO, buffer, SECTOR_SIZE / 2);  // SECTOR_SIZE / 2 for 16-bit transfers

    // Print the data in hexadecimal format for verification
    printf("Data read from FDD sector:\n");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", (unsigned char)buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");  // New line every 16 bytes for readability
    }

    // Turn off the motor after the read is complete
    fdd_motor_off(drive);

    return true;
}

// Writes a sector to the FDD
bool fdd_write_sector(int drive, int head, int track, int sector, uint8_t* buffer){
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