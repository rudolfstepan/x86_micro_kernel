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

volatile bool irq_triggered = false;

// FDC IRQ handler for IRQ6
void fdd_irq_handler(uint8_t* r) {
    // Read the FDC main status register to confirm it's a valid interrupt
    uint8_t status = inb(0x3F4);
    if (!(status & 0x10)) {  // Check specific bits indicating valid state
        // Ignore if the interrupt is not valid for current operation
        return;
    }
    //printf("IRQ 6 handler invoked\n");
    // Setzen einer globalen Variable, um anzuzeigen, dass der IRQ ausgelöst wurde
    irq_triggered = true;
    outb(0x20, 0x20); // Send EOI to PIC
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

// void fdc_initialize_irq(){
//     irq_install_handler(6, irq6_handler);  // Install IRQ6 handler for FDD
// }

void fdc_initialize() {
    outb(0x3F2, 0x1C);  // Aktiviert den Motor und das Laufwerk A
    sleep_ms(50);         // Wartet, bis der Motor läuft
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
void fdd_motor_on(int drive) {
    outb(FDD_DOR, 0x1C | drive);  // Motor on, drive select
    sleep_ms(50);
}

// Turn off the FDD motor
void fdd_motor_off(int drive) {
    outb(FDD_DOR, drive & 0xFC);  // Motor off
}

// Wait for FDC to signal readiness
bool wait_for_fdc_ready() {
    int timeout = 100000;
    while (!(fdc_get_status() & 0x80)) {
        if (--timeout == 0) {
            printf("Timeout waiting for FDC.\n");
            return false;
        }
    }
    return true;
}

// Wait for FDC interrupt to indicate completion
bool fdc_wait_for_irq() {
    irq_triggered = false;  // Reset the IRQ flag before waiting
    int timeout = 10000;    // Arbitrary timeout value to avoid infinite wait
    while (!irq_triggered && timeout-- > 0) {
        __asm__ __volatile__("pause");  // Reduce CPU load
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

bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    // clear the buffer before reading
    memset(buffer, 0, SECTOR_SIZE);  // Clear the buffer before reading
    // prepare DMA for reading
    dma_prepare_floppy(buffer, SECTOR_SIZE, true);
    // Clear any pending IRQs from previous operations
    irq_triggered = false;  // Reset IRQ flag before issuing command
    fdc_wait_for_irq();
    // Start motor and wait for it to stabilize
    fdd_motor_on(drive);
    sleep_ms(50);  // Delay to allow motor to spin up and avoid spurious IRQs

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
        fdd_motor_off(drive);
        return false;
    }
    // Turn off the motor after the operation
    fdd_motor_off(drive);
    sleep_ms(50);
    // Clear FDC state after read
    //fdc_reset_after_read();
    // Optional: Delay between commands to stabilize FDC for next operation
    //sleep_ms(500);
    return true;
}

void debug_read_bootsector(uint8_t sector) {
    unsigned char buffer[512];
    // Attempt to read the first sector (boot sector) of the floppy disk
    if (fdc_read_sector(0, 0, 0, sector, buffer)) {
        printf("Boot sector read successfully:\n");
        // Print the boot sector content in hexadecimal
        hex_dump(buffer, 512);
    } else {
        printf("Failed to read boot sector.\n");
    }
    return;
}

// Write a sector to the FDD
bool fdd_write_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    if (!buffer) return false;

    fdd_motor_on(drive);  // Turn on motor

    if (!wait_for_fdc_ready()) {
        printf("FDC not ready for WRITE command.\n");
        fdd_motor_off(drive);
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
        fdd_motor_off(drive);
        return false;
    }

    //fdc_wait();  // Wait until FDC is ready to accept data
    outsw(FDD_FIFO, buffer, SECTOR_SIZE / 2);  // Transfer buffer to FDC FIFO

    fdd_motor_off(drive);  // Turn off motor
    return true;
}

// Detect and initialize all FDD drives
void fdd_detect_drives() {
    for (uint8_t drive = FDD_DRIVE_A; drive <= FDD_DRIVE_B; drive++) {
        fdd_motor_on(drive);  // Turn on motor for the current drive

        // Check if the FDC is ready for the current drive
        if (fdc_get_status(drive) & 0x80) {  // Example condition: modify based on actual status check logic
            drive_t* detected_drive = (drive_t*)malloc(sizeof(drive_t));
            detected_drive->type = DRIVE_TYPE_FDD;
            detected_drive->fdd_drive_no = drive;
            snprintf(detected_drive->name, sizeof(detected_drive->name), "fdd%d", drive);
            detected_drive->cylinder = 80;  // Typical number of cylinders for a 3.5" floppy drive
            detected_drive->head = 2;       // Number of heads (double-sided)
            detected_drive->sector = 18;    // Typical number of sectors per track for a 1.44 MB disk

            printf("Floppy drive detected: %s\n", detected_drive->name);
            detected_drives[drive_count++] = *detected_drive;
        } else {
            printf("No floppy drive detected at fdd%d.\n", drive);
        }

        fdd_motor_off(drive);  // Turn off motor for the current drive
    }

    if (drive_count == 0) {
        printf("No floppy drives detected.\n");
    }
}