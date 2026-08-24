/**
 * @file drivers/block/fdd.c
 * @brief FDC-Erkennung, DMA-Transfer und begrenzte Recovery.
 *
 * Layer: Ring-0 block and bus driver.
 * Contract: Ressourcen, LBA-Bereiche und Backendbesitz werden vor jedem Seiteneffekt validiert.
 * Safety: Timeout und Medienwechsel brechen I/O ab, statt stale Daten zu akzeptieren.
 */
#include "fdd.h"
#include "ata.h"
#include "arch/x86/include/sys.h"
#include "drivers/char/io.h"
#include "include/kernel/panic.h"
#include "include/kernel/storage_safety.h"
#include "include/kernel/storage_service.h"
#include "kernel/sched/scheduler.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "lib/libc/string.h"
#include "kernel/time/pit.h"

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
#define PIC1_DATA            0x21
#define CMOS_ADDRESS         0x70
#define CMOS_DATA            0x71
#define CMOS_FLOPPY_TYPES    0x10
#define SECTOR_SIZE          512
#define FDD_MAX_TRANSFER     (18U * SECTOR_SIZE)

#define DMA_CHANNEL_MASK 0x0A
#define DMA_MODE 0x0B
#define DMA_CLEAR 0x0C
#define DMA_ADDR_PORT 0x04
#define DMA_COUNT_PORT 0x05
#define DMA_PAGE_PORT 0x81
#define DMA_UNMASK_CHANNEL 0x02

#define FDD_DRIVE_A 0
#define FDD_DRIVE_B 1

#define MSR_RQM 0x80
#define MSR_DIO 0x40
#define MSR_CB  0x10

static volatile bool irq_triggered = false;
static volatile int irq_count = 0;
static bool fdc_controller_initialized = false;
static bool fdc_drive_ready[4] = {false, false, false, false};
static bool fdd_motor_running[MAX_FDD_DRIVES];
static int8_t fdd_cached_track[MAX_FDD_DRIVES] = {-1, -1};
static uint32_t fdd_last_activity[MAX_FDD_DRIVES];
static volatile bool fdd_write_fenced;

static int fdd_resource_index(uint8_t drive) {
    for (short index = 0; index < drive_count; ++index) {
        if (detected_drives[index].type == DRIVE_TYPE_FDD &&
            detected_drives[index].fdd_drive_no == drive) return index;
    }
    return -1;
}

/* The FDC command FIFO, DMA channel and completion flag form one synchronous
 * transaction domain.  Keep IRQ6 enabled for completion and prevent another
 * task from interleaving controller state.  The scheduler counter is nestable
 * because public convenience operations call other public FDC operations. */
static void fdd_transaction_begin(void) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    scheduler_preempt_disable();
}

static void fdd_transaction_end(void) {
    KASSERT_NOT_IRQ();
    KASSERT(irq_enabled());
    KASSERT(scheduler_preempt_is_disabled());
    scheduler_preempt_enable();
}

#define FDD_MOTOR_SPINUP_MS 500U
#define FDD_MOTOR_IDLE_MS 2000U

/* ISA DMA channel 2 can only address the first 16 MiB and may not cross a
 * 64-KiB boundary.  Reserve enough storage to select a 64-KiB boundary at
 * runtime; the selected address is still checked before programming the 8237. */
static uint8_t fdd_dma_storage[0x10000U + FDD_MAX_TRANSFER]
    __attribute__((aligned(16)));

static uint8_t *fdd_dma_buffer(void) {
    uintptr_t start = (uintptr_t)fdd_dma_storage;
    if (start > ~(uintptr_t)0 - 0xFFFFu) return NULL;
    return (uint8_t *)((start + 0xFFFFu) & ~(uintptr_t)0xFFFFu);
}

static uint8_t fdd_cmos_configuration(void) {
    outb(CMOS_ADDRESS, CMOS_FLOPPY_TYPES);
    return inb(CMOS_DATA);
}

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
static bool fdc_initialize_impl(void) {
    if (register_interrupt_handler(6, (void*)fdd_irq_handler) != 0) {
        printf("Failed to register FDC IRQ6 handler.\n");
        return false;
    }
    unmask_irq6(); // ✅ was missing previously
    return true;
}

bool fdc_initialize(void) {
    fdd_transaction_begin();
    bool result = fdc_initialize_impl();
    fdd_transaction_end();
    return result;
}

uint8_t fdc_get_status() { return inb(FDD_MSR); }

void print_fdc_status() {
    printf("FDC Status: 0x%X\n", fdc_get_status());
}

// ============================================================
// FIX #2: Correct DOR motor control bits
// ============================================================
static void fdc_motor_on_impl(int drive) {
    if (drive < 0 || drive >= MAX_FDD_DRIVES) return;
    // bits 4..7 select motors A..D; bits 0..1 select the active drive.
    uint8_t dor = (uint8_t)(0x0C | (drive & 0x03) | (0x10u << drive));
    outb(FDD_DOR, dor);
    fdd_motor_running[drive] = true;
    fdd_last_activity[drive] = pit_ticks();
}

void fdc_motor_on(int drive) {
    fdd_transaction_begin();
    fdc_motor_on_impl(drive);
    fdd_transaction_end();
}

static void fdc_motor_off_impl(int drive) {
    if (drive < 0 || drive >= MAX_FDD_DRIVES) return;
    uint8_t dor = inb(FDD_DOR);
    dor &= (uint8_t)~(0x10u << drive);
    dor = (uint8_t)((dor & ~0x03u) | (drive & 0x03) | 0x0C);
    outb(FDD_DOR, dor);
    fdd_motor_running[drive] = false;
    fdd_cached_track[drive] = -1;
}

void fdc_motor_off(int drive) {
    fdd_transaction_begin();
    fdc_motor_off_impl(drive);
    fdd_transaction_end();
}

static void fdd_prepare_drive(uint8_t drive) {
    if (!fdd_motor_running[drive]) {
        fdc_motor_on(drive);
        delay_ms(FDD_MOTOR_SPINUP_MS);
    }
    fdd_last_activity[drive] = pit_ticks();
}

static void fdd_service_impl(void) {
    uint32_t now = pit_ticks();
    for (uint8_t drive = 0; drive < MAX_FDD_DRIVES; ++drive) {
        if (fdd_motor_running[drive] &&
            (uint32_t)(now - fdd_last_activity[drive]) >= FDD_MOTOR_IDLE_MS) {
            fdc_motor_off(drive);
        }
    }
}

void fdd_service(void) {
    fdd_transaction_begin();
    fdd_service_impl();
    fdd_transaction_end();
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
static bool fdc_reset_controller_impl(void) {
    fdc_controller_initialized = false;
    outb(FDD_DOR, 0x00);
    delay_ms(20);
    outb(FDD_DOR, 0x0C);
    delay_ms(20);
    outb(FDD_CCR, 0x00);

    for (uint8_t drive = 0; drive < MAX_FDD_DRIVES; ++drive) {
        fdd_motor_running[drive] = false;
        fdd_cached_track[drive] = -1;
    }
    for (int interrupt = 0; interrupt < 4; ++interrupt) {
        irq_triggered = false;
        if (!fdc_send_command(FDC_SENSE_INTERRUPT_CMD)) return false;
        (void)fdc_read_data();
        (void)fdc_read_data();
    }
    if (!fdc_send_command(0x03) || !fdc_send_command(0xDF) ||
        !fdc_send_command(0x02)) return false;
    fdc_controller_initialized = true;
    return true;
}

// ============================================================
// DMA preparation (unchanged)
// ============================================================
static bool dma_prepare_floppy(uint8_t* buffer, uint16_t length, bool read) {
    uintptr_t physical = (uintptr_t)buffer;
    if (!buffer || length == 0 ||
        physical > 0x00FFFFFFu ||
        (uintptr_t)(length - 1) > (0x00FFFFFFu - physical) ||
        ((physical & 0xFFFFu) + length) > 0x10000u) {
        printf("FDD DMA buffer is outside ISA DMA limits (addr=0x%08X, len=%u)\n",
               (uint32_t)physical, length);
        return false;
    }

    outb(DMA_CHANNEL_MASK, 0x06);
    outb(DMA_CLEAR, 0x00);

    uint16_t address = (uint16_t)(physical & 0xFFFFu);
    outb(DMA_ADDR_PORT, address & 0xFF);
    outb(DMA_ADDR_PORT, (address >> 8) & 0xFF);

    uint8_t page = (uint8_t)((physical >> 16) & 0xFFu);
    outb(DMA_PAGE_PORT, page);

    uint16_t count = length - 1;
    outb(DMA_COUNT_PORT, count & 0xFF);
    outb(DMA_COUNT_PORT, (count >> 8) & 0xFF);

    outb(DMA_MODE, read ? 0x46 : 0x4A);
    outb(DMA_CHANNEL_MASK, DMA_UNMASK_CHANNEL);
    return true;
}

// ============================================================
// Controller init (✅ add CCR + interrupt clear loop fix)
// ============================================================
static bool fdc_init_controller_impl(void) {
    if (fdc_controller_initialized) return true;
    printf("Initializing FDC controller...\n");

    if (!fdc_initialize()) {
        return false;
    }

    if (!fdc_reset_controller_impl()) {
        printf("Failed to reset and configure FDC controller.\n");
        return false;
    }
    printf("FDC controller initialized.\n");
    return true;
}

bool fdc_init_controller(void) {
    fdd_transaction_begin();
    bool result = fdc_init_controller_impl();
    fdd_transaction_end();
    return result;
}

// ============================================================
// Everything else unchanged below this line
// ============================================================

static bool fdc_seek(uint8_t drive, uint8_t head, uint8_t track) {
    if (fdd_cached_track[drive] == (int8_t)track) return true;
    irq_triggered = false;
    if (!fdc_send_command(FDD_CMD_SEEK) ||
        !fdc_send_command((uint8_t)((head << 2) | drive)) ||
        !fdc_send_command(track) ||
        !fdc_wait_for_irq() ||
        !fdc_send_command(FDC_SENSE_INTERRUPT_CMD)) {
        return false;
    }

    uint8_t st0 = fdc_read_data();
    uint8_t cylinder = fdc_read_data();
    bool ok = (st0 & 0xC0u) == 0 && (st0 & 0x20u) != 0 && cylinder == track;
    if (ok) {
        fdd_cached_track[drive] = (int8_t)track;
    } else {
        fdd_cached_track[drive] = -1;
    }
    return ok;
}

static bool fdc_validate_chs(uint8_t drive, uint8_t head, uint8_t track,
                             uint8_t sector, const void *buffer) {
    return buffer != NULL && drive < MAX_FDD_DRIVES && head < 2 &&
           track < 80 && sector >= 1 && sector <= 18;
}

static bool fdc_read_sectors_impl(uint8_t drive, uint8_t head, uint8_t track,
                                  uint8_t sector, uint8_t count,
                                  void* buffer) {
    if (!fdc_validate_chs(drive, head, track, sector, buffer) || count == 0 ||
        count > 18 || (uint16_t)sector + count - 1U > 18U) return false;

    fdd_prepare_drive(drive);

    if (!fdc_seek(drive, head, track)) {
        printf("FDC seek failed before read (drive=%u, head=%u, track=%u)\n",
               drive, head, track);
        fdc_motor_off(drive);
        return false;
    }

    if (!fdc_wait_write(100)) {
        printf("FDC not ready to read sector.\n");
        fdc_motor_off(drive);
        return false;
    }

    uint8_t *dma_buffer = fdd_dma_buffer();
    uint16_t transfer_size = (uint16_t)count * SECTOR_SIZE;
    if (!dma_prepare_floppy(dma_buffer, transfer_size, true)) {
        fdc_motor_off(drive);
        return false;
    }
    irq_triggered = false;

    if (!fdc_send_command(FDD_CMD_READ) ||
        !fdc_send_command((head << 2) | (drive & 0x03)) ||
        !fdc_send_command(track) ||
        !fdc_send_command(head) ||
        !fdc_send_command(sector) ||
        !fdc_send_command(2) ||
        !fdc_send_command((uint8_t)(sector + count - 1U)) ||
        !fdc_send_command(0x1B) ||
        !fdc_send_command(0xFF)) {
        printf("Failed to send READ command sequence.\n");
        fdc_motor_off(drive);
        return false;
    }

    if (!fdc_wait_for_irq()) {
        printf("Timeout waiting for FDC read completion.\n");
        fdc_motor_off(drive);
        return false;
    }

    uint8_t st0 = fdc_read_data();
    uint8_t st1 = fdc_read_data();
    uint8_t st2 = fdc_read_data();
    fdc_read_data(); fdc_read_data(); fdc_read_data(); fdc_read_data();

    if ((st0 & 0xC0) != 0) {
        printf("FDC read error: ST0=0x%x, ST1=0x%x, ST2=0x%x\n", st0, st1, st2);
        fdc_motor_off(drive);
        return false;
    }

    memcpy(buffer, dma_buffer, transfer_size);
    fdd_last_activity[drive] = pit_ticks();
    return true;
}

bool fdc_read_sectors(uint8_t drive, uint8_t head, uint8_t track,
                      uint8_t sector, uint8_t count, void* buffer) {
    fdd_transaction_begin();
    int resource = fdd_resource_index(drive);
    bool attempted = resource >= 0 &&
        storage_service_resource_available((uint32_t)resource);
    bool result = attempted &&
        fdc_read_sectors_impl(drive, head, track, sector, count, buffer);
    if (attempted && !result)
        (void)storage_service_report_io_failure((uint32_t)resource);
    fdd_transaction_end();
    return result;
}

bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track,
                     uint8_t sector, void* buffer) {
    fdd_transaction_begin();
    bool result = fdc_read_sectors(drive, head, track, sector, 1, buffer);
    fdd_transaction_end();
    return result;
}

bool fdc_read_sector_recovery(uint8_t drive, uint8_t head, uint8_t track,
                              uint8_t sector, void* buffer) {
    fdd_transaction_begin();
    bool result = fdc_read_sectors_impl(drive, head, track, sector, 1U,
                                        buffer);
    fdd_transaction_end();
    return result;
}

static bool fdc_write_sectors_impl(uint8_t drive, uint8_t head, uint8_t track,
                                   uint8_t sector, uint8_t count,
                                   const void* buffer) {
    if (!fdc_validate_chs(drive, head, track, sector, buffer) || count == 0 ||
        count > 18 || (uint16_t)sector + count - 1U > 18U) return false;

    uint8_t *dma_buffer = fdd_dma_buffer();
    uint16_t transfer_size = (uint16_t)count * SECTOR_SIZE;
    memcpy(dma_buffer, buffer, transfer_size);
    fdd_prepare_drive(drive);

    if (!fdc_seek(drive, head, track) ||
        !dma_prepare_floppy(dma_buffer, transfer_size, false)) {
        fdc_motor_off(drive);
        return false;
    }

    irq_triggered = false;
    if (!fdc_send_command(FDD_CMD_WRITE) ||
        !fdc_send_command((uint8_t)((head << 2) | drive)) ||
        !fdc_send_command(track) ||
        !fdc_send_command(head) ||
        !fdc_send_command(sector) ||
        !fdc_send_command(2) ||
        !fdc_send_command((uint8_t)(sector + count - 1U)) ||
        !fdc_send_command(0x1B) ||
        !fdc_send_command(0xFF) ||
        !fdc_wait_for_irq()) {
        fdc_motor_off(drive);
        return false;
    }

    uint8_t st0 = fdc_read_data();
    uint8_t st1 = fdc_read_data();
    uint8_t st2 = fdc_read_data();
    fdc_read_data(); fdc_read_data(); fdc_read_data(); fdc_read_data();
    fdd_last_activity[drive] = pit_ticks();

    if ((st0 & 0xC0u) != 0 || st1 != 0 || st2 != 0) {
        printf("FDC write error: ST0=0x%x, ST1=0x%x, ST2=0x%x\n", st0, st1, st2);
        fdc_motor_off(drive);
        return false;
    }
    return true;
}

bool fdc_write_sectors(uint8_t drive, uint8_t head, uint8_t track,
                       uint8_t sector, uint8_t count, const void* buffer) {
    fdd_transaction_begin();
    int resource = fdd_resource_index(drive);
    bool armed = !fdd_write_fenced && resource >= 0 &&
        storage_write_begin((uint32_t)resource, pit_monotonic_ms());
    bool result = armed && fdc_write_sectors_impl(drive, head, track, sector,
                                                  count, buffer);
    if (armed && !storage_write_end(result)) result = false;
    fdd_transaction_end();
    return result;
}

bool fdd_write_sector(uint8_t drive, uint8_t head, uint8_t track,
                      uint8_t sector, const void* buffer) {
    fdd_transaction_begin();
    bool result = fdc_write_sectors(drive, head, track, sector, 1, buffer);
    fdd_transaction_end();
    return result;
}

void fdd_fence_writes(void) {
    fdd_write_fenced = true;
    __asm__ volatile("" ::: "memory");
    if (!fdc_controller_initialized) return;
    outb(FDD_DOR, (uint8_t)(inb(FDD_DOR) & 0x0FU));
    for (uint8_t drive = 0; drive < MAX_FDD_DRIVES; ++drive) {
        fdd_motor_running[drive] = false;
        fdd_cached_track[drive] = -1;
    }
}

void fdd_restore_writes_after_recovery(void) {
    fdd_write_fenced = false;
    __asm__ volatile("" ::: "memory");
}

bool fdd_writes_quiescent(void) {
    if (!fdd_write_fenced) return false;
    if (!fdc_controller_initialized) return true;
    return (inb(FDD_DOR) & 0xF0U) == 0U && (inb(FDD_MSR) & MSR_CB) == 0U;
}

static bool fdc_calibrate_drive_impl(uint8_t drive) {
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
            (void)fdc_reset_controller_impl();
            continue;
        }

        // Sense Interrupt Status (0x08) -> ST0, current cylinder
        if (!fdc_send_command(0x08)) {
            printf("fdc_calibrate_drive: SENSE INTERRUPT send failed (attempt %d)\n", attempt);
            fdc_motor_off(drive);
            (void)fdc_reset_controller_impl();
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
        (void)fdc_reset_controller_impl();
        delay_ms(100);
    }

    printf("fdc_calibrate_drive: FAILED after %d attempts (drive %u)\n", max_retries, drive);
    return false;
}

bool fdc_calibrate_drive(uint8_t drive) {
    fdd_transaction_begin();
    bool result = fdc_calibrate_drive_impl(drive);
    fdd_transaction_end();
    return result;
}

bool fdc_requalify_drive(uint8_t drive) {
    fdd_transaction_begin();
    bool result = drive < MAX_FDD_DRIVES && fdc_reset_controller_impl() &&
                  fdc_calibrate_drive_impl(drive);
    fdd_transaction_end();
    return result;
}

// =============================================================
// Fixed FDD drive detection (keeps same signature & behavior)
// =============================================================
static void fdd_detect_drives_impl(void) {
    uint8_t cmos_drives = fdd_cmos_configuration();
    if (cmos_drives == 0) {
        printf("No floppy drives configured; skipping FDC.\n");
        return;
    }
    printf("Detecting floppy drives...\n");

    // Ensure FDC is initialized and IRQs are active
    if (!fdc_controller_initialized) {
        if (!fdc_init_controller()) {
            printf("FDC initialization failed. Cannot detect drives.\n");
            return;
        }
    }

    int fdd_count = 0;

    // QEMU and PCs normally expose 0 or 1 floppy drive (A:)
    for (uint8_t drive = FDD_DRIVE_A; drive <= FDD_DRIVE_B; drive++) {
        uint8_t drive_type = drive == FDD_DRIVE_A
            ? (uint8_t)(cmos_drives >> 4)
            : (uint8_t)(cmos_drives & 0x0F);
        if (drive_type == 0) continue;
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

        if (drive_count < 0 || drive_count >= MAX_DRIVES) {
            printf("Drive registry is full; cannot add fdd%u.\n", drive);
            fdc_motor_off(drive);
            break;
        }

        drive_t *detected_drive = &detected_drives[drive_count];
        memset(detected_drive, 0, sizeof(*detected_drive));
        detected_drive->type = DRIVE_TYPE_FDD;
        detected_drive->fdd_drive_no = drive;
        snprintf(detected_drive->name, sizeof(detected_drive->name), "fdd%d", drive);
        detected_drive->cylinder = 80;  // 80 tracks
        detected_drive->head = 2;       // double-sided
        detected_drive->sector = 18;    // 18 sectors per track
        detected_drive->sectors = detected_drive->cylinder *
            detected_drive->head * detected_drive->sector;
        drive_count++;
        fdd_count++;

        printf("Detected floppy drive: fdd%d (A:%c)\n", drive, 'A' + drive);
        fdc_motor_off(drive);
    }

    if (fdd_count == 0)
        printf("No floppy drives detected.\n");
    else
        printf("%d floppy drive(s) initialized.\n", fdd_count);
}

void fdd_detect_drives(void) {
    fdd_transaction_begin();
    fdd_detect_drives_impl();
    fdd_transaction_end();
}

// =============================================================
// Debug: Read and dump the boot sector of a floppy drive
// =============================================================
void debug_read_bootsector(void) {
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

