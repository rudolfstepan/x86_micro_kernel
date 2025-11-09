#include "fdd.h"
#include "ata.h"
#include "arch/x86/sys.h"
#include "drivers/char/io.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define FDD_CMD_RECALIBRATE  0x07
#define FDD_CMD_SEEK         0x0F
#define FDD_CMD_READ         0xE6
#define FDD_CMD_WRITE        0xC5
#define FDD_DRIVE            0x00
#define FDD_DOR              0x3F2
#define FDD_MSR              0x3F4
#define FDD_FIFO             0x3F5
#define FDD_CCR              0x3F7   // ✅ added: Control Configuration Register
#define PIC1_COMMAND         0x20
#define PIC1_DATA            0x21
#define PIC_EOI              0x20
#define SECTOR_SIZE          512

#define DMA_CHANNEL_MASK 0x0A
#define DMA_MODE 0x0B
#define DMA_CLEAR 0x0C
#define DMA_ADDR_PORT 0x04
#define DMA_COUNT_PORT 0x05
#define DMA_PAGE_PORT 0x81
#define DMA_UNMASK_CHANNEL 0x02

#define MAX_FDD_DRIVES 2
#define FDD_DRIVE_A 0
#define FDD_DRIVE_B 1

#define MSR_RQM 0x80
#define MSR_DIO 0x40
#define MSR_CB  0x10

static volatile bool irq_triggered = false;
static volatile int irq_count = 0;
static bool fdc_controller_initialized = false;
static bool fdc_drive_ready[4] = {false, false, false, false};

// ============================================================
// FIX #1: Correct FIFO readiness functions
// ============================================================
static inline bool fdc_ready_to_write(void) {
    uint8_t msr = inb(FDD_MSR);
    return (msr & MSR_RQM) && !(msr & MSR_DIO);
}

static inline bool fdc_ready_to_read(void) {
    uint8_t msr = inb(FDD_MSR);
    return (msr & MSR_RQM) && (msr & MSR_DIO);
}

static bool fdc_wait_write(uint32_t ms) {
    while (ms--) { if (fdc_ready_to_write()) return true; delay_ms(1); }
    return false;
}
static bool fdc_wait_read(uint32_t ms) {
    while (ms--) { if (fdc_ready_to_read()) return true; delay_ms(1); }
    return false;
}

// ============================================================
// IRQ handler
// ============================================================
void fdd_irq_handler(uint8_t* r) {
    irq_triggered = true;
    irq_count++;
    outb(PIC1_COMMAND, PIC_EOI);
}

// ============================================================
// IRQ mask helpers
// ============================================================
void unmask_irq6() {
    uint8_t mask = inb(PIC1_DATA);
    mask &= ~(1 << 6);
    outb(PIC1_DATA, mask);
}
void mask_irq6() {
    uint8_t mask = inb(PIC1_DATA);
    mask |= (1 << 6);
    outb(PIC1_DATA, mask);
}

// ============================================================
// Init IRQ handler + unmask (✅ FIXED)
// ============================================================
void fdc_initialize() {
    syscall(SYS_INSTALL_IRQ, (void*)6, (uint8_t*)fdd_irq_handler, 0);
    unmask_irq6(); // ✅ was missing previously
}

uint8_t fdc_get_status() { return inb(FDD_MSR); }

void print_fdc_status() {
    printf("FDC Status: 0x%X\n", fdc_get_status());
}

// ============================================================
// FIX #2: Correct DOR motor control bits
// ============================================================
void fdc_motor_on(int drive) {
    // bit 3=enable IRQ/DMA, bit 2=reset=1, bits 4-7=motors
    outb(FDD_DOR, 0x1C | (drive & 0x03));
}
void fdc_motor_off(int drive) {
    // keep controller enabled, disable only motor
    outb(FDD_DOR, 0x0C | (drive & 0x03));
}

// ============================================================
// Wait for IRQ completion (unchanged, but simplified)
// ============================================================
bool fdc_wait_for_irq() {
    int timeout = 3000;
    while (timeout--) {
        if (irq_triggered) return true;
        delay_ms(1);
    }
    printf("Timeout waiting for FDC IRQ.\n");
    return false;
}

// ============================================================
// FIX #3: FIFO-based command send using readiness polling
// ============================================================
bool fdc_send_command(uint8_t command) {
    if (!fdc_wait_write(100)) {
        printf("FDC not ready for command 0x%02X\n", command);
        return false;
    }
    outb(FDD_FIFO, command);
    return true;
}

// ============================================================
// FIX #4: Proper FIFO read using readiness polling
// ============================================================
uint8_t fdc_read_data() {
    if (!fdc_wait_read(100)) {
        printf("FDC read timeout.\n");
        return 0xFF;
    }
    return inb(FDD_FIFO);
}

// ============================================================
// Full controller reset (with CCR setup ✅ FIX)
// ============================================================
void fdc_full_reset() {
    outb(FDD_DOR, 0x00);
    delay_ms(20);
    outb(FDD_DOR, 0x0C);
    delay_ms(20);
    outb(FDD_CCR, 0x00);  // ✅ 500 kbps (1.44MB drive)
}

// ============================================================
// DMA preparation (unchanged)
// ============================================================
void dma_prepare_floppy(uint8_t* buffer, uint16_t length, bool read) {
    outb(DMA_CHANNEL_MASK, 0x06);
    outb(DMA_CLEAR, 0x00);

    uint16_t address = (uint32_t)buffer & 0xFFFF;
    outb(DMA_ADDR_PORT, address & 0xFF);
    outb(DMA_ADDR_PORT, (address >> 8) & 0xFF);

    uint8_t page = ((uint32_t)buffer >> 16) & 0xFF;
    outb(DMA_PAGE_PORT, page);

    uint16_t count = length - 1;
    outb(DMA_COUNT_PORT, count & 0xFF);
    outb(DMA_COUNT_PORT, (count >> 8) & 0xFF);

    outb(DMA_MODE, read ? 0x46 : 0x4A);
    outb(DMA_CHANNEL_MASK, DMA_UNMASK_CHANNEL);
}

// ============================================================
// Controller init (✅ add CCR + interrupt clear loop fix)
// ============================================================
bool fdc_init_controller() {
    printf("Initializing FDC controller...\n");

    fdc_initialize(); // ✅ ensure IRQ installed

    outb(FDD_DOR, 0x00);
    delay_ms(10);
    outb(FDD_DOR, 0x0C);
    delay_ms(10);

    outb(FDD_CCR, 0x00);  // ✅ set 500 kbps data rate

    // Clear post-reset interrupts
    for (int i = 0; i < 4; i++) {
        irq_triggered = false;
        delay_ms(10);
        fdc_send_command(0x08);
        fdc_read_data();
        fdc_read_data();
    }

    if (!fdc_send_command(0x03) ||
        !fdc_send_command(0xDF) ||
        !fdc_send_command(0x02)) {
        printf("Failed to send SPECIFY command.\n");
        return false;
    }

    printf("FDC controller initialized.\n");
    return true;
}

// ============================================================
// Everything else unchanged below this line
// ============================================================

bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track, uint8_t sector, void* buffer) {
    if (!fdc_wait_write(100)) {
        printf("FDC not ready to read sector.\n");
        return false;
    }

    dma_prepare_floppy((uint8_t*)buffer, SECTOR_SIZE, true);
    irq_triggered = false;

    if (!fdc_send_command(FDD_CMD_READ) ||
        !fdc_send_command((head << 2) | (drive & 0x03)) ||
        !fdc_send_command(track) ||
        !fdc_send_command(head) ||
        !fdc_send_command(sector) ||
        !fdc_send_command(2) ||
        !fdc_send_command(18) ||
        !fdc_send_command(0x1B) ||
        !fdc_send_command(0xFF)) {
        printf("Failed to send READ command sequence.\n");
        return false;
    }

    if (!fdc_wait_for_irq()) {
        printf("Timeout waiting for FDC read completion.\n");
        return false;
    }

    uint8_t st0 = fdc_read_data();
    uint8_t st1 = fdc_read_data();
    uint8_t st2 = fdc_read_data();
    fdc_read_data(); fdc_read_data(); fdc_read_data(); fdc_read_data();

    if ((st0 & 0xC0) != 0) {
        printf("FDC read error: ST0=0x%x, ST1=0x%x, ST2=0x%x\n", st0, st1, st2);
        return false;
    }

    return true;
}

bool fdc_calibrate_drive(uint8_t drive) {
    const int max_retries = 3;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        // Spin up motor and clear IRQ state
        fdc_motor_on(drive);
        delay_ms(500);
        irq_triggered = false;

        // Send RECALIBRATE (0x07), then drive number
        if (!fdc_send_command(FDD_CMD_RECALIBRATE) ||
            !fdc_send_command(drive & 0x03)) {
            printf("fdc_calibrate_drive: send RECALIBRATE failed (attempt %d)\n", attempt);
            fdc_motor_off(drive);
            continue;
        }

        // Wait for IRQ6 from the controller
        if (!fdc_wait_for_irq()) {
            printf("fdc_calibrate_drive: IRQ timeout (attempt %d)\n", attempt);
            fdc_motor_off(drive);
            // Controller might be confused; try a full reset before next attempt
            fdc_full_reset();
            continue;
        }

        // Sense Interrupt Status (0x08) -> ST0, current cylinder
        if (!fdc_send_command(0x08)) {
            printf("fdc_calibrate_drive: SENSE INTERRUPT send failed (attempt %d)\n", attempt);
            fdc_motor_off(drive);
            fdc_full_reset();
            continue;
        }

        uint8_t st0 = fdc_read_data();
        uint8_t cyl = fdc_read_data();

        // Done with the motor for this attempt
        fdc_motor_off(drive);

        // Success when seek complete bit is set and cylinder is 0
        if ((st0 & 0x20) && cyl == 0) {
            printf("fdc_calibrate_drive: OK (drive %u, ST0=0x%02X, CYL=%u)\n", drive, st0, cyl);
            return true;
        }

        printf("fdc_calibrate_drive: not at CYL0 (drive %u, ST0=0x%02X, CYL=%u, attempt %d)\n",
               drive, st0, cyl, attempt);

        // Try again after a brief pause and controller reset
        fdc_full_reset();
        delay_ms(100);
    }

    printf("fdc_calibrate_drive: FAILED after %d attempts (drive %u)\n", max_retries, drive);
    return false;
}

// =============================================================
// Fixed FDD drive detection (keeps same signature & behavior)
// =============================================================
void fdd_detect_drives() {
    printf("Detecting floppy drives...\n");

    // Ensure FDC is initialized and IRQs are active
    if (!fdc_controller_initialized) {
        if (!fdc_init_controller()) {
            printf("FDC initialization failed. Cannot detect drives.\n");
            return;
        }
        fdc_controller_initialized = true;
    }

    // QEMU and PCs normally expose 0 or 1 floppy drive (A:)
    for (uint8_t drive = FDD_DRIVE_A; drive <= FDD_DRIVE_B; drive++) {
        printf("Probing drive %d...\n", drive);

        // Spin up motor
        fdc_motor_on(drive);
        delay_ms(500);

        // Attempt a calibration to confirm drive presence
        if (!fdc_calibrate_drive(drive)) {
            printf("Drive %d not responding or no media.\n", drive);
            fdc_motor_off(drive);
            continue;
        }

        // Controller now calibrated, read MSR to verify responsiveness
        uint8_t msr = fdc_get_status();
        if ((msr & 0x80) == 0) {
            printf("Drive %d not ready (MSR=0x%02X)\n", drive, msr);
            fdc_motor_off(drive);
            continue;
        }

        // Allocate and register detected drive
        drive_t* detected_drive = (drive_t*)malloc(sizeof(drive_t));
        if (!detected_drive) {
            printf("Memory allocation failed for drive %d.\n", drive);
            fdc_motor_off(drive);
            continue;
        }

        detected_drive->type = DRIVE_TYPE_FDD;
        detected_drive->fdd_drive_no = drive;
        snprintf(detected_drive->name, sizeof(detected_drive->name), "fdd%d", drive);
        detected_drive->cylinder = 80;  // 80 tracks
        detected_drive->head = 2;       // double-sided
        detected_drive->sector = 18;    // 18 sectors per track
        detected_drive->mount_point[0] = '\0';

        detected_drives[drive_count++] = *detected_drive;
        free(detected_drive);

        printf("Detected floppy drive: fdd%d (A:%c)\n", drive, 'A' + drive);
        fdc_motor_off(drive);
    }

    if (drive_count == 0)
        printf("No floppy drives detected.\n");
    else
        printf("%d floppy drive(s) initialized.\n", drive_count);
}

// =============================================================
// Debug: Read and dump the boot sector of a floppy drive
// =============================================================
void debug_read_bootsector() {
    uint8_t drive = 0;   // A:
    uint8_t head  = 0;
    uint8_t track = 0;
    uint8_t sector = 1;  // Boot sector is sector 1 on track 0

    printf("\n=== DEBUG: Boot Sector Dump (FDD%d, Track=%u, Head=%u, Sector=%u) ===\n",
           drive, track, head, sector);

    uint8_t* buffer = (uint8_t*)malloc(SECTOR_SIZE);
    if (!buffer) {
        printf("debug_read_bootsector: Memory allocation failed.\n");
        return;
    }

    // Read sector 1
    bool ok = fdc_read_sector(drive, head, track, sector, buffer);
    if (!ok) {
        printf("debug_read_bootsector: Failed to read boot sector.\n");
        free(buffer);
        return;
    }

    // Pretty-print 16 bytes per line
    printf("Offset   Hexadecimal Data                                     ASCII\n");
    printf("--------------------------------------------------------------------------\n");

    for (int i = 0; i < SECTOR_SIZE; i += 16) {
        // Print offset
        printf("%04X  ", i);

        // Print hex bytes
        for (int j = 0; j < 16; j++) {
            if (i + j < SECTOR_SIZE)
                printf("%02X ", buffer[i + j]);
            else
                printf("   "); // padding for last line
        }

        printf(" ");

        // Print ASCII representation
        for (int j = 0; j < 16 && (i + j) < SECTOR_SIZE; j++) {
            uint8_t c = buffer[i + j];
            if (c >= 32 && c <= 126)
                printf("%c", c);
            else
                printf(".");
        }

        printf("\n");
    }

    printf("--------------------------------------------------------------------------\n");
    printf("End of boot sector (512 bytes total)\n\n");

    free(buffer);
}

