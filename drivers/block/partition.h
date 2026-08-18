/**
 * @file drivers/block/partition.h
 * @brief MBR/GPT-Erkennung und begrenzte Partitionsbereitstellung.
 *
 * Layer: Ring-0 block and bus driver.
 * Contract: Ressourcen, LBA-Bereiche und Backendbesitz werden vor jedem Seiteneffekt validiert.
 * Safety: Partitionen dürfen nie außerhalb des validierten Elternmediums liegen.
 */
#ifndef PARTITION_H
#define PARTITION_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/bus/drives.h"

/* Discover bounded MBR or CRC-validated GPT partitions on whole media. */
size_t partition_discover(void);

/* Create one verified, aligned MBR partition on an empty whole disk. */
int partition_provision_mbr(drive_t *drive, uint32_t first_lba,
                            uint32_t sectors, uint8_t type);

#endif
