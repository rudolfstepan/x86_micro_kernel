#ifndef PARTITION_H
#define PARTITION_H

#include <stddef.h>

/* Discover bounded primary MBR partitions on ATA/AHCI whole-media devices.
 * Invalid, overlapping and protective-GPT layouts publish no child devices. */
size_t partition_discover_mbr(void);

#endif
