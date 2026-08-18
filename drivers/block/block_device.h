/**
 * @file drivers/block/block_device.h
 * @brief Einheitliche Sektor-I/O-Schnittstelle aller Blockbackends.
 *
 * Layer: Ring-0 block and bus driver.
 * Contract: Ressourcen, LBA-Bereiche und Backendbesitz werden vor jedem Seiteneffekt validiert.
 * Safety: Operationen liefern explizite Statuswerte und respektieren Partitionsgrenzen.
 */
#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "drivers/bus/drives.h"

typedef enum {
    BLOCK_DEVICE_OK = 0,
    BLOCK_DEVICE_INVALID = -22,
    BLOCK_DEVICE_RANGE = -34,
    BLOCK_DEVICE_IO = -5,
    BLOCK_DEVICE_UNSUPPORTED = -95,
} block_device_result_t;

/* Transport-neutral, one-sector-at-a-time contract. The caller owns the
 * buffer; implementations must validate the complete range before I/O. */
int block_device_read_sector(const drive_t *drive, uint32_t sector,
                             void *buffer);
int block_device_write_sector(const drive_t *drive, uint32_t sector,
                              const void *buffer);
int block_device_read_sectors(const drive_t *drive, uint32_t sector,
                              uint32_t count, void *buffer);
int block_device_write_sectors(const drive_t *drive, uint32_t sector,
                               uint32_t count, const void *buffer);
int block_device_flush(const drive_t *drive);
bool block_device_sector_range_valid(const drive_t *drive, uint32_t sector);

#endif
