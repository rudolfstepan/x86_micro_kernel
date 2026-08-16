#include "block_device.h"

#include "ata.h"
#include "ahci.h"
#include "fdd.h"
#include "lib/libc/string.h"

bool block_device_sector_range_valid(const drive_t *drive, uint32_t sector) {
    return drive != NULL && drive->sectors != 0U && sector < drive->sectors;
}

static const drive_t *partition_parent(const drive_t *drive,
                                       uint32_t sector,
                                       uint32_t *parent_sector) {
    if (drive == NULL || parent_sector == NULL ||
        drive->type != DRIVE_TYPE_PARTITION ||
        drive->parent_resource >= (uint32_t)drive_count ||
        !block_device_sector_range_valid(drive, sector)) return NULL;
    const drive_t *parent = &detected_drives[drive->parent_resource];
    if (parent->type == DRIVE_TYPE_PARTITION ||
        drive->lba_offset >= parent->sectors ||
        sector > UINT32_MAX - drive->lba_offset) return NULL;
    uint32_t absolute = drive->lba_offset + sector;
    if (!block_device_sector_range_valid(parent, absolute)) return NULL;
    *parent_sector = absolute;
    return parent;
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
    if (drive->type == DRIVE_TYPE_PARTITION) {
        uint32_t parent_sector;
        const drive_t *parent = partition_parent(drive, sector,
                                                  &parent_sector);
        return parent == NULL ? BLOCK_DEVICE_RANGE :
            block_device_read_sector(parent, parent_sector, buffer);
    }
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
    if (drive->type == DRIVE_TYPE_PARTITION) {
        uint32_t parent_sector;
        const drive_t *parent = partition_parent(drive, sector,
                                                  &parent_sector);
        return parent == NULL ? BLOCK_DEVICE_RANGE :
            block_device_write_sector(parent, parent_sector, buffer);
    }
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

static bool block_device_range_valid(const drive_t *drive, uint32_t sector,
                                     uint32_t count) {
    return drive != NULL && count != 0U && count <= ATA_PIO_MAX_SECTORS &&
           sector < drive->sectors && count <= drive->sectors - sector;
}

int block_device_read_sectors(const drive_t *drive, uint32_t sector,
                              uint32_t count, void *buffer) {
    if (buffer == NULL || !block_device_range_valid(drive, sector, count))
        return BLOCK_DEVICE_RANGE;
    if (drive->type == DRIVE_TYPE_PARTITION) {
        uint32_t parent_sector;
        const drive_t *parent = partition_parent(drive, sector,
                                                  &parent_sector);
        if (parent == NULL || count > parent->sectors - parent_sector)
            return BLOCK_DEVICE_RANGE;
        return block_device_read_sectors(parent, parent_sector, count, buffer);
    }
    if (drive->type == DRIVE_TYPE_ATA)
        return ata_read_sectors(drive->base, sector, count, buffer,
                                drive->is_master)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        int result = block_device_read_sector(drive, sector + index,
                                               bytes + index * 512U);
        if (result != BLOCK_DEVICE_OK) return result;
    }
    return BLOCK_DEVICE_OK;
}

int block_device_write_sectors(const drive_t *drive, uint32_t sector,
                               uint32_t count, const void *buffer) {
    if (buffer == NULL || !block_device_range_valid(drive, sector, count))
        return BLOCK_DEVICE_RANGE;
    if (drive->type == DRIVE_TYPE_PARTITION) {
        uint32_t parent_sector;
        const drive_t *parent = partition_parent(drive, sector,
                                                  &parent_sector);
        if (parent == NULL || count > parent->sectors - parent_sector)
            return BLOCK_DEVICE_RANGE;
        return block_device_write_sectors(parent, parent_sector, count,
                                          buffer);
    }
    if (drive->type == DRIVE_TYPE_ATA)
        return ata_write_sectors(drive->base, sector, count, buffer,
                                 drive->is_master)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    const uint8_t *bytes = buffer;
    for (uint32_t index = 0U; index < count; ++index) {
        int result = block_device_write_sector(drive, sector + index,
                                                bytes + index * 512U);
        if (result != BLOCK_DEVICE_OK) return result;
    }
    return BLOCK_DEVICE_OK;
}

int block_device_flush(const drive_t *drive) {
    if (drive == NULL) return BLOCK_DEVICE_INVALID;
    if (drive->type == DRIVE_TYPE_PARTITION) {
        if (drive->parent_resource >= (uint32_t)drive_count)
            return BLOCK_DEVICE_INVALID;
        const drive_t *parent = &detected_drives[drive->parent_resource];
        if (parent->type == DRIVE_TYPE_PARTITION) return BLOCK_DEVICE_INVALID;
        return block_device_flush(parent);
    }
    if (drive->type == DRIVE_TYPE_ATA)
        return ata_flush_cache(drive->base, drive->is_master)
            ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_AHCI)
        return ahci_flush(drive) ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO;
    if (drive->type == DRIVE_TYPE_FDD) return BLOCK_DEVICE_OK;
    return BLOCK_DEVICE_UNSUPPORTED;
}
