#include "fdd.h"
#include "drivers/io/io.h"
#include "toolchain/stdio.h"
#include "kernel/system.h"
#include "kernel/sys.h"

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

volatile bool irq_triggered = false;

// FDC IRQ handler for IRQ6
void irq6_handler(struct regs* r) {
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

void fdc_initialize_irq(){
    irq_install_handler(6, irq6_handler);  // Install IRQ6 handler for FDD
}

void fdc_initialize() {
    outb(0x3F2, 0x1C);  // Aktiviert den Motor und das Laufwerk A
    delay(500);         // Wartet, bis der Motor läuft
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
    delay(5);
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
    delay(10);          // Small delay for reset to take effect
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
    delay(5000);  // Delay to allow motor to spin up and avoid spurious IRQs

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
    delay(5000);
    // Clear FDC state after read
    //fdc_reset_after_read();
    // Optional: Delay between commands to stabilize FDC for next operation
    //delay(500);
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

// Detect and initialize the FDD drive
void fdd_detect_drives() {
    fdd_motor_on(FDD_DRIVE);  // Turn on motor

    if (fdc_get_status() & 0x80) {  // Check if FDC is ready
        drive_t* drive = (drive_t*)malloc(sizeof(drive_t));
        drive->type = DRIVE_TYPE_FDD;
        snprintf(drive->name, sizeof(drive->name), "fdd%d", 1);
        drive->cylinder = 80;
        drive->head = 2;
        drive->sector = 18;
        printf("Floppy drive detected: %s\n", drive->name);
        detected_drives[drive_count++] = *drive;
    } else {
        printf("No floppy drive detected.\n");
    }

    fdd_motor_off(FDD_DRIVE);  // Turn off motor
}
