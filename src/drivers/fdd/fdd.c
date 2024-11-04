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

#define DMA_CHANNEL_2 0x02
#define DMA_PAGE_REG 0x81
#define DMA_ADDRESS_REG 0x04
#define DMA_COUNT_REG 0x05
#define DMA_MASK_REG 0x0A
#define DMA_MODE_REG 0x0B
#define DMA_MODE_READ 0x46
#define DMA_MODE_WRITE 0x4A

// Static buffer for DMA transfer
static unsigned char dma_buffer[512] __attribute__((aligned(512)));
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

void delay(int milliseconds) {
    volatile int count = milliseconds * 1000;
    while (count--) {
        __asm__ __volatile__("nop"); // No-operation; consumes CPU cycles
    }
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

void dma_prepare_transfer(uint16_t length, bool read) {
    // Maskiert DMA-Kanal 2
    outb(0x0A, 0x06);  // Kanal 2 maskieren

    // DMA-Buffer-Adresse festlegen
    uint16_t address = (uint32_t)dma_buffer & 0xFFFF;
    uint8_t page = ((uint32_t)dma_buffer >> 16) & 0xFF;

    outb(0x04, address & 0xFF);        // LSB der Adresse
    outb(0x04, (address >> 8) & 0xFF); // MSB der Adresse
    outb(0x81, page);                  // Setzt die Seitenadresse

    // Anzahl der Bytes für Übertragung (count - 1)
    uint16_t count = length - 1;
    outb(0x05, count & 0xFF);           // LSB der Anzahl
    outb(0x05, (count >> 8) & 0xFF);    // MSB der Anzahl

    // Setzt den Modus für Lesen oder Schreiben
    uint8_t mode = read ? 0x46 : 0x4A;
    outb(0x0B, mode | 0x02);            // Modus und Kanal

    // Hebt die Maskierung für DMA-Kanal 2 auf
    outb(0x0A, 0x02);  // Unmask channel 2

    //printf("dma_prepare_transfer done\n");
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

bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    // Clear any pending IRQs from previous operations
    fdc_wait_for_irq();
    
    // Step 1: Reset the FDC and ignore any initial/spurious IRQ
    //fdc_reset();
    //printf("FDC reset completed.\n");
    // Step 1: Perform a full FDC reset
    fdc_full_reset();
    printf("FDC full reset completed.\n");

    fdc_wait_for_irq();  // Clear any pending IRQ 6

    // Step 2: Clear any residual data from the FDC data register
    //fdc_clear_data_register();

    // Step 3: Prepare the DMA transfer with a cleared buffer
    dma_reset_channel();  // Ensure DMA channel is reset

    // Step 2: Prepare the DMA transfer with a fresh buffer
    memset(dma_buffer, 0, 512);       // Clear buffer
    dma_prepare_transfer(512, true);  // Prepare DMA for read
    //printf("DMA prepare transfer done.\n");

    // Step 3: Start motor and wait for it to stabilize
    fdd_motor_on(drive);
    delay(1000);  // Delay to allow motor to spin up and avoid spurious IRQs
    //printf("Floppy motor on.\n");

    // Step 4: Reset IRQ flag and send the command sequence to FDC
    irq_triggered = false;  // Reset IRQ flag before issuing command
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

    //printf("Waiting for IRQ 6 after issuing READ command...\n");

    // Step 5: Wait for IRQ 6, indicating the transfer is complete
    //that does not work and is commented out because the irq has been fired previously!
    // if (!fdc_wait_for_irq()) {
    //     printf("Error: No response from IRQ 6.\n");
    //     fdd_motor_off(drive);
    //     return false;
    // }
    // printf("IRQ 6 received, FDC read complete.\n");

    memcpy(buffer, dma_buffer, 512);

    // Step 6: Turn off the motor after the operation
    fdd_motor_off(drive);

    // Step 7: Clear FDC state after read
    fdc_reset_after_read();

    // Optional: Delay between commands to stabilize FDC for next operation
    delay(10);

    return true;
}

void debug_read_bootsector() {
    unsigned char buffer[512];
    // Attempt to read the first sector (boot sector) of the floppy disk
    if (fdc_read_sector(0, 0, 0, 1, buffer)) {
        printf("Boot sector read successfully:\n");
        // Print the boot sector content in hexadecimal
        hex_dump(buffer, 256);
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
