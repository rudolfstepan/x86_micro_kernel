/*
 * =============================================================
 * Floppy Disk Drive (FDD) Controller and DMA Handling Module
 * =============================================================
 * 
 * This module provides low-level functions for interacting with
 * a floppy disk drive (FDD) and controlling Direct Memory Access (DMA)
 * operations. It enables basic operations such as reading and writing
 * sectors, managing the FDD motor, handling IRQs, and configuring the
 * DMA controller for data transfers.
 *
 * This code is designed for use in a bare-metal environment or kernel,
 * where direct interaction with hardware registers is necessary.
 *
 * =============================================================
 * Key Components and Functionality
 * =============================================================
 * 
 * 1. **Floppy Disk Commands and Registers**:
 *    - **Digital Output Register (DOR)**: Controls the FDD motor and drive selection.
 *    - **Main Status Register (MSR)**: Provides the status of the FDD.
 *    - **FIFO Register**: Used to send commands and data to/from the FDD.
 * 
 * 2. **DMA Configuration for FDD**:
 *    - **DMA Channel Mask and Mode**: Configures the DMA channel for reading/writing.
 *    - **DMA Address and Count Ports**: Sets the memory address and byte count for data transfer.
 *    - **Page Register**: Configures the page for addressing memory beyond 64KB.
 *    - **DMA Clear and Unmask**: Clears and unblocks DMA channels as necessary.
 *
 * 3. **FDD Motor Control**:
 *    - **fdd_motor_on(drive)**: Turns on the FDD motor for the specified drive.
 *    - **fdd_motor_off(drive)**: Turns off the FDD motor.
 * 
 * 4. **IRQ Handling**:
 *    - **IRQ6**: The FDD interrupt is mapped to IRQ6. The `irq6_handler` function
 *      handles FDD interrupts, setting a flag (`irq_triggered`) to indicate completion.
 *    - **mask_irq6()** and **unmask_irq6()**: Manage the IRQ6 line on the Programmable
 *      Interrupt Controller (PIC) to enable/disable FDD interrupts.
 * 
 * 5. **Low-Level FDD Operations**:
 *    - **fdc_send_command(command)**: Sends a command to the FDD via the FIFO register.
 *    - **fdc_read_sector(drive, head, track, sector, buffer)**: Reads a sector from the FDD,
 *      using DMA and handling the motor, command sequence, and IRQ waiting.
 *    - **fdc_reset()** and **fdc_full_reset()**: Reset the FDD to a known state.
 * 
 * =============================================================
 * Detailed Process for Reading a Sector from the FDD
 * =============================================================
 * 
 * The `fdc_read_sector` function follows these steps to read a sector:
 * 
 * 1. **Prepare DMA for Read Operation**:
 *    - The `dma_prepare_floppy` function configures DMA for the read operation by setting
 *      the memory address, byte count, and mode register (read mode).
 * 
 * 2. **Turn On FDD Motor**:
 *    - The `fdd_motor_on` function activates the FDD motor and selects the drive.
 *    - A short delay is used to allow the motor to stabilize.
 * 
 * 3. **Clear Previous IRQ State**:
 *    - The `irq_triggered` flag is reset to ensure the FDD interrupt for this operation is captured.
 * 
 * 4. **Send FDD Read Command Sequence**:
 *    - The `fdc_send_command` function issues a command sequence to the FDD via the FIFO
 *      register. This includes specifying the head, track, sector, and sector size.
 * 
 * 5. **Wait for IRQ Completion**:
 *    - The `fdc_wait_for_irq` function waits for the IRQ6 interrupt to indicate the
 *      completion of the read operation. The IRQ handler sets `irq_triggered` when triggered.
 * 
 * 6. **Turn Off FDD Motor**:
 *    - After the read operation, the motor is turned off to save power.
 * 
 * =============================================================
 * DMA Configuration for Floppy Disk Data Transfers
 * =============================================================
 * 
 * The `dma_prepare_floppy` function configures DMA for data transfer to/from the FDD:
 * 
 * 1. **Mask DMA Channel**: Masks the DMA channel 2 to prepare for configuration.
 * 
 * 2. **Clear Flip-Flop Register**: Clears the internal flip-flop to ensure correct
 *    byte alignment for address and count registers.
 * 
 * 3. **Set Buffer Address and Page**: Configures the low and high bytes of the buffer
 *    address, as well as the page register for addressing beyond 64KB.
 * 
 * 4. **Set Byte Count**: Sets the number of bytes to transfer (length - 1).
 * 
 * 5. **Set Mode Register**: Configures the DMA mode register for read or write.
 * 
 * 6. **Unmask DMA Channel**: Unmasks the DMA channel, enabling it for data transfer.
 * 
 * =============================================================
 * Example Usage: Reading the Boot Sector
 * =============================================================
 * 
 * The `debug_read_bootsector` function demonstrates how to read the boot sector
 * from the FDD:
 * 
 * 1. **Prepare the Buffer**: Allocates a buffer to hold 512 bytes (one sector).
 * 
 * 2. **Issue Read Command**: Calls `fdc_read_sector` to read the specified sector.
 * 
 * 3. **Print Data**: If successful, prints the contents of the sector in hex format.
 * 
 * =============================================================
 * Summary of Constants and Macros
 * =============================================================
 * - **FDD_CMD_RECALIBRATE**, **FDD_CMD_SEEK**, **FDD_CMD_READ**, **FDD_CMD_WRITE**:
 *   Commands used to control the FDD.
 * - **DMA_CHANNEL_MASK**, **DMA_MODE**, **DMA_CLEAR**: DMA configuration constants.
 * - **PIC_EOI**: End-of-Interrupt command for the PIC.
 * - **SECTOR_SIZE**: Size of a sector in bytes, typically 512 bytes for FDDs.
 * 
 * =============================================================
 * Dependencies
 * =============================================================
 * - `io.h`: Provides low-level I/O functions for port access.
 * - `stdio.h`: Basic input/output functions for debugging.
 * - `system.h` and `sys.h`: Kernel system utilities.
 * 
 */

#include "fdd.h"
#include "drivers/io/io.h"
#include "toolchain/stdio.h"
#include "toolchain/stdlib.h"
#include "toolchain/strings.h"


#define FDD_CMD_RECALIBRATE  0x07
#define FDD_CMD_SEEK         0x0F
#define FDD_CMD_READ         0xE6
#define FDD_CMD_WRITE        0xC5
#define FDD_DRIVE            0x00   // Drive 0 for A:
#define FDD_DOR              0x3F2  // Digital Output Register
#define FDD_MSR              0x3F4  // Main Status Register
#define FDD_FIFO             0x3F5  // Data Register
#define PIC1_COMMAND         0x20   // Primary PIC command port
#define PIC1_DATA            0x21   // PIC data port
#define PIC_EOI              0x20   // End of Interrupt command
#define SECTOR_SIZE          512

#define DMA_CHANNEL_MASK 0x0A
#define DMA_MODE 0x0B
#define DMA_CLEAR 0x0C
#define DMA_ADDR_PORT 0x04
#define DMA_COUNT_PORT 0x05
#define DMA_PAGE_PORT 0x81
#define DMA_UNMASK_CHANNEL 0x02

#define MAX_FDD_DRIVES 2  // Maximum number of floppy disk drives (A: and B:)
#define FDD_DRIVE_A 0     // Drive number for A:
#define FDD_DRIVE_B 1     // Drive number for B:

static volatile bool irq_triggered = false;

// FDC IRQ handler for IRQ6
void fdd_irq_handler(uint8_t* r) {
    uint8_t status = inb(FDD_MSR);  // Read the Main Status Register (MSR)

    // Print the status for debugging
    //printf("ISR invoked. MSR Status: 0x%X\n", status);

    // Check if bit 4 is set (indicating a valid interrupt)
    if (status & 0x10) {
        irq_triggered = true;  // Set IRQ flag when valid
        //printf("Valid interrupt detected for FDD.\n");
    } else {
        //printf("Unexpected interrupt state. Status: 0x%X\n", status);
    }

    // Send EOI to the PIC to acknowledge the interrupt
    outb(PIC1_COMMAND, PIC_EOI);
}

// Unmask IRQ6 (for FDD) on the PIC
void unmask_irq6() {
    uint8_t mask = inb(0x21);   // Read the current mask on the master PIC
    mask &= ~(1 << 6);          // Clear the bit for IRQ 6
    outb(0x21, mask);           // Write the new mask
}

// Mask IRQ6 (for FDD) on the PIC
void mask_irq6() {
    uint8_t mask = inb(PIC1_DATA);
    mask |= (1 << 6);  // Set bit 6 to mask IRQ6
    outb(PIC1_DATA, mask);
}

void fdc_initialize() {
    // outb(0x3F2, 0x1C);  // Aktiviert den Motor und das Laufwerk A
    // sleep_ms(50);         // Wartet, bis der Motor läuft
    mask_irq6();                            // Ensure IRQ6 is unmasked
}

// Get the FDC status
uint8_t fdc_get_status() {
    return inb(FDD_MSR);
}

// Print FDC status (for debugging)
void print_fdc_status() {
    printf("FDC Status: 0x%X\n", fdc_get_status());
}

// Turn on the FDD motor and select the specified drive
void fdc_motor_on(int drive) {
    outb(FDD_DOR, 0x1C | drive);  // Motor on, drive select
}

// Turn off the FDD motor
void fdc_motor_off(int drive) {
    outb(FDD_DOR, drive & 0xFC);  // Motor off
}

// Wait for FDC to signal readiness
bool wait_for_fdc_ready() {
    int timeout = 1000;  // Adjusted for a shorter wait period (1000 ms total)
    while (!(fdc_get_status() & 0x80)) {  // Wait for bit 7 of the MSR to be set
        if (--timeout == 0) {
            printf("Timeout waiting for FDC ready signal.\n");
            return false;
        }
        sleep_ms(1);  // Avoid CPU-intensive busy waiting
    }
    return true;
}

// Wait for FDC interrupt to indicate completion
bool fdc_wait_for_irq() {
    irq_triggered = false;  // Reset IRQ flag before waiting
    int timeout = 1000;     // 1000 ms timeout for IRQ wait

    printf("Waiting for IRQ...\n");

    while (!irq_triggered && timeout-- > 0) {
        sleep_ms(2);

        // uint8_t msr = inb(FDD_MSR);
        // if(msr & 0x10){
        //         irq_triggered = true;
        //         break;
        //     }

        // if (timeout % 100 == 0) {  // Print status periodically for debugging
        //     printf("Waiting... MSR Status: 0x%X\n", msr);
        // }
    }
    

    if (!irq_triggered) {
        printf("IRQ wait timed out.\n");
    } else {
        printf("IRQ detected.\n");
    }
    return irq_triggered;
}

// Send a command to the FDC via the FIFO
bool fdc_send_command(uint8_t command) {
    if (!wait_for_fdc_ready()) return false;
    outb(FDD_FIFO, command);
    return true;
}

void fdc_reset() {
    // Issue the Sense Interrupt Status command to clear any pending IRQs
    fdc_send_command(0x08);  // Sense Interrupt command
    // Read the two bytes of status that the FDC returns for this command
    inb(0x3F5);  // Dummy read to clear status
    inb(0x3F5);  // Dummy read to clear status
}

void fdc_reset_after_read() {
    // Issue the Sense Interrupt Status command (0x08) to clear any pending states
    fdc_send_command(0x08);
    // Read the two bytes of status that the FDC returns for this command
    inb(0x3F5);  // Dummy read to clear status
    inb(0x3F5);  // Dummy read to clear status
}
void fdc_full_reset() {
    // Send a reset signal to the FDC (0x3F2 port)
    outb(0x3F2, 0x00);  // Disable the motor and reset
    sleep_ms(50);          // Small delay for reset to take effect
    outb(0x3F2, 0x0C);  // Re-enable motor and set drive select to 0
}
void fdc_clear_data_register() {
    while (inb(0x3F4) & 0x80) {
        inb(0x3F5);  // Read data register until it is cleared
    }
}
void dma_reset_channel() {
    outb(0x0A, 0x06);  // Mask DMA channel 2
    outb(0x0A, 0x02);  // Unmask DMA channel 2
}

void dma_prepare_floppy(uint8_t* buffer, uint16_t length, bool read) {
    // 1. Kanal 2 maskieren
    outb(DMA_CHANNEL_MASK, 0x06);  // Maske auf Kanal 2 setzen

    // 2. Clear Flip-Flop Register
    outb(DMA_CLEAR, 0x00);

    // 3. Adresse des Puffers setzen
    uint16_t address = (uint32_t)buffer & 0xFFFF;
    outb(DMA_ADDR_PORT, address & 0xFF);         // Niedriges Byte der Adresse
    outb(DMA_ADDR_PORT, (address >> 8) & 0xFF);  // Hohes Byte der Adresse

    // 4. Seitenadresse setzen
    uint8_t page = ((uint32_t)buffer >> 16) & 0xFF;
    outb(DMA_PAGE_PORT, page);

    // 5. Anzahl der Bytes setzen (Länge - 1)
    uint16_t count = length - 1;
    outb(DMA_COUNT_PORT, count & 0xFF);         // Niedriges Byte der Anzahl
    outb(DMA_COUNT_PORT, (count >> 8) & 0xFF);  // Hohes Byte der Anzahl

    // 6. Modusregister setzen für Lese-/Schreiboperation
    outb(DMA_MODE, read ? 0x46 : 0x4A);  // 0x46 für Lesen, 0x4A für Schreiben

    // 7. Kanal 2 wieder aktivieren (unmaskieren)
    outb(DMA_CHANNEL_MASK, DMA_UNMASK_CHANNEL);
}

bool _fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    printf("Reading sector %d from track %d, head %d, drive %d...\n", sector, track, head, drive);

    // clear the buffer before reading
    memset(buffer, 0, SECTOR_SIZE);  // Clear the buffer before reading

    //fdc_wait_for_irq();  // Wait for any pending IRQs to clear
    sleep_ms(10);  // Small delay before issuing command
    // prepare DMA for reading
    dma_prepare_floppy(buffer, SECTOR_SIZE, true);

    sleep_ms(10);  // Small delay before issuing command

    // Reset IRQ flag and send the command sequence to FDC
    //irq_triggered = false;  // Reset IRQ flag before issuing command
    if (!fdc_send_command(FDD_CMD_READ) ||
        !fdc_send_command((head << 2) | (drive & 0x03)) ||
        !fdc_send_command(track) ||
        !fdc_send_command(head) ||
        !fdc_send_command(sector) ||
        !fdc_send_command(2) ||  // Sector size
        !fdc_send_command(18) || // Last sector
        !fdc_send_command(0x1B) || // Gap length
        !fdc_send_command(0xFF)) {  // Unused byte
        printf("Failed to send READ command sequence.\n");
        fdc_motor_off(drive);
        return false;
    }
    return true;
}

bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    fdc_motor_on(drive);  // Turn on the motor
    sleep_ms(500);        // Wait for the motor to spin up

    bool result = _fdc_read_sector(drive, head, track, sector, buffer);

    fdc_motor_off(drive); // Turn off the motor after the operation
    return result;
}

bool fdc_read_sectors(uint8_t drive, uint8_t head, uint8_t track, uint8_t start_sector, uint8_t num_sectors, void* buffer) {
    fdc_motor_on(drive);  // Turn on the motor
    sleep_ms(500);        // Wait for the motor to spin up

    for (uint8_t sector = start_sector; sector < start_sector + num_sectors; sector++) {
        if (!_fdc_read_sector(drive, head, track, sector, buffer)) {
            printf("Failed to read sector %d.\n", sector);
            fdc_motor_off(drive);  // Turn off the motor if an error occurs
            return false;
        }
        buffer += SECTOR_SIZE;  // Move to the next buffer location
    }

    fdc_motor_off(drive); // Turn off the motor after reading all sectors
    return true;
}

void debug_read_bootsector(uint8_t sector) {

    uint8_t* buffer = (uint8_t*)malloc(SECTOR_SIZE);
    if (!buffer) {
        printf("Memory allocation failed for sector buffer.\n");
        return;
    }

    memset(buffer, 0xff, SECTOR_SIZE);  // Clear the buffer before reading


    // Attempt to read the first sector (boot sector) of the floppy disk
    if (fdc_read_sector(0, 0, 0, sector, buffer)) {
        printf("Boot sector read successfully:\n");
        // Print the boot sector content in hexadecimal
        hex_dump(buffer, SECTOR_SIZE);
    } else {
        printf("Failed to read boot sector.\n");
    }

    free(buffer);  // Free the buffer after use

    return;
}

// Write a sector to the FDD
bool fdd_write_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    if (!buffer) return false;

    fdc_motor_on(drive);  // Turn on motor

    if (!wait_for_fdc_ready()) {
        printf("FDC not ready for WRITE command.\n");
        fdc_motor_off(drive);
        return false;
    }

    printf("Issuing WRITE command sequence...\n");

    if (!fdc_send_command(FDD_CMD_WRITE) ||
        !fdc_send_command((head << 2) | (drive & 0x03)) ||
        !fdc_send_command(track) ||
        !fdc_send_command(head) ||
        !fdc_send_command(sector) ||
        !fdc_send_command(2) ||  // Sector size
        !fdc_send_command(18) || // Last sector in track
        !fdc_send_command(0x1B) || // Gap length
        !fdc_send_command(0xFF)) { // Data length
        printf("Failed to send WRITE command sequence.\n");
        fdc_motor_off(drive);
        return false;
    }

    //fdc_wait();  // Wait until FDC is ready to accept data
    outsw(FDD_FIFO, buffer, SECTOR_SIZE / 2);  // Transfer buffer to FDC FIFO

    fdc_motor_off(drive);  // Turn off motor
    return true;
}

// Helper function to send the drive number (0 for A, 1 for B)
void fdc_send_drive(uint8_t drive) {
    // Ensure that the drive number is within the valid range (0 or 1)
    if (drive > 1) {
        printf("Invalid drive number: %d. Only 0 (A) or 1 (B) are supported.\n", drive);
        return;
    }

    // The drive number should be sent after the recalibration command,
    // which is part of the command format for recalibrating or selecting drives.
    uint8_t drive_select = drive == 0 ? 0x00 : 0x01;  // 0x00 for A, 0x01 for B

    // Wait until the controller is ready to accept a command (bit 7 of MSR set)
    while (!(inb(FDC_BASE + 4) & 0x80)) {
        // Busy-wait until ready
        sleep_ms(1);
    }

    // Send the drive number to the Data Register
    outb(FDC_BASE + 5, drive_select);
}

// Helper function to read data from the FDC
uint8_t fdc_read_data() {
    // Wait until the controller is ready to provide data (bit 7 of MSR)
    while (!(inb(FDC_BASE + 4) & 0x80)) {
        // Busy-wait until the data register is ready to be read
        sleep_ms(1);
    }

    // Read from the Data Register (usually located at FDC_BASE + 5)
    return inb(FDC_BASE + 5);
}

// Recalibrate the specified drive (0 for A, 1 for B)
bool fdc_recalibrate(uint8_t drive) {
    return true;

    if (!fdc_send_command(FDD_CMD_RECALIBRATE)) {
        printf("Failed to send recalibrate command for drive %d.\n", drive);
        return false;
    }
    fdc_send_drive(drive);

    sleep_ms(100);  // Initial delay to allow recalibration to start

    for (int i = 0; i < 1000; i++) {  // Timeout loop with a total of 1000 ms
        sleep_ms(1);
        uint8_t msr = inb(FDC_BASE + 4);
        if ((msr & 0x10) == 0x10) {  // Check if the drive is ready (bit 4 set)
            fdc_send_command(FDC_SENSE_INTERRUPT_CMD);
            uint8_t st0 = fdc_read_data();  // Read ST0 register
            uint8_t cylinder = fdc_read_data();  // Read current cylinder number

            if ((st0 & 0xC0) == 0 && cylinder == 0) {
                return true;  // Recalibration successful
            }
        }
    }

    printf("Recalibration timed out for drive %d.\n", drive);
    return false;
}

// Detect and initialize all FDD drives
void fdd_detect_drives() {
    for (uint8_t drive = FDD_DRIVE_A; drive <= FDD_DRIVE_B; drive++) {
        fdc_motor_on(drive);  // Turn on motor for the current drive
        
        // Issue a recalibrate command to ensure the drive is functioning
        if (fdc_recalibrate(drive)) {
            // Check if the FDC is ready and if the drive responds correctly
            uint8_t status = fdc_get_status(drive);
            if ((status & 0x80) && !(status & 0x10)) {  // Check ready bit and ensure no error bit is set
                drive_t* detected_drive = (drive_t*)malloc(sizeof(drive_t));
                if (detected_drive == NULL) {
                    printf("Memory allocation failed for detected drive.\n");
                    fdc_motor_off(drive);
                    continue;
                }

                detected_drive->type = DRIVE_TYPE_FDD;
                detected_drive->fdd_drive_no = drive;
                snprintf(detected_drive->name, sizeof(detected_drive->name), "fdd%d", drive);
                detected_drive->cylinder = 80;  // Typical number of cylinders for a 3.5" floppy drive
                detected_drive->head = 2;       // Number of heads (double-sided)
                detected_drive->sector = 18;    // Typical number of sectors per track for a 1.44 MB disk

                printf("Floppy drive detected: %s\n", detected_drive->name);
                detected_drives[drive_count++] = *detected_drive;

                // Free the temporary detected_drive as we already copied its content
                free(detected_drive);
            } else {
                printf("No valid response from floppy drive at fdd%d.\n", drive);
            }
        } else {
            printf("Recalibrate failed for fdd%d. No drive detected.\n", drive);
        }

        fdc_motor_off(drive);  // Turn off motor for the current drive
    }

    if (drive_count == 0) {
        printf("No floppy drives detected.\n");
    }
}
