#include "block_device.h"

#include "ata.h"
#include "ahci.h"
#include "fdd.h"
#include "lib/libc/string.h"

bool block_device_sector_range_valid(const drive_t *drive, uint32_t sector) {
    return drive != NULL && drive->sectors != 0U && sector < drive->sectors;
}

static int fdd_chs(const drive_t *drive, uint32_t sector,
                   uint8_t *head, uint8_t *track, uint8_t *number) {
    if (drive == NULL || head == NULL || track == NULL || number == NULL ||
        drive->head == 0U || drive->sector == 0U ||
        sector >= drive->sectors) return BLOCK_DEVICE_INVALID;
    uint32_t track_size = drive->head * drive->sector;
    uint32_t cylinder = sector / track_size;
    uint32_t within = sector % track_size;
    uint32_t selected_head = within / drive->sector;
    uint32_t selected_sector = within % drive->sector + 1U;
    if (cylinder >= drive->cylinder || selected_head > 255U ||
        selected_sector > 255U) return BLOCK_DEVICE_RANGE;
    *head = (uint8_t)selected_head;
    *track = (uint8_t)cylinder;
    *number = (uint8_t)selected_sector;
    return BLOCK_DEVICE_OK;
}

int block_device_read_sector(const drive_t *drive, uint32_t sector,
                             void *buffer) {
    if (buffer == NULL || !block_device_sector_range_valid(drive, sector))
        return BLOCK_DEVICE_INVALID;
    if (drive->type == DRIVE_TYPE_ATA)
        return ata_read_sector_fresh(drive->base, sector, buffer,
                                     drive->is_master)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_AHCI)
        return ahci_read_sector(drive, sector, buffer)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_FDD) {
        uint8_t head, track, number;
        int result = fdd_chs(drive, sector, &head, &track, &number);
        return result != BLOCK_DEVICE_OK ? result :
            (fdc_read_sector(drive->fdd_drive_no, head, track, number, buffer)
                ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO);
    }
    return BLOCK_DEVICE_UNSUPPORTED;
}

int block_device_write_sector(const drive_t *drive, uint32_t sector,
                              const void *buffer) {
    if (buffer == NULL || !block_device_sector_range_valid(drive, sector))
        return BLOCK_DEVICE_INVALID;
    if (drive->type == DRIVE_TYPE_ATA)
        return ata_write_sector(drive->base, sector, (void *)buffer,
                                drive->is_master)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_AHCI)
        return ahci_write_sector(drive, sector, buffer)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_FDD) {
        uint8_t head, track, number;
        int result = fdd_chs(drive, sector, &head, &track, &number);
        return result != BLOCK_DEVICE_OK ? result :
            (fdc_write_sectors(drive->fdd_drive_no, head, track, number, 1U,
                                buffer) ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO);
    }
    return BLOCK_DEVICE_UNSUPPORTED;
}

int block_device_flush(const drive_t *drive) {
    if (drive == NULL) return BLOCK_DEVICE_INVALID;
    if (drive->type == DRIVE_TYPE_ATA)
        return ata_flush_cache(drive->base, drive->is_master)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_AHCI)
        return ahci_flush(drive) ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_FDD) return BLOCK_DEVICE_OK;
    return BLOCK_DEVICE_UNSUPPORTED;
}
