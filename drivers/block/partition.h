#ifndef PARTITION_H
#define PARTITION_H

#include <stddef.h>

/* Discover bounded MBR or CRC-validated GPT partitions on whole media. */
size_t partition_discover(void);

#endif
