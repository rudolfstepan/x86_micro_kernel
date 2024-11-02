#ifndef FDD_H
#define FDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool fdd_read_sector(unsigned int cylinder, unsigned int head, unsigned int sector, void* buffer);
bool fdd_write_sector(unsigned int cylinder, unsigned int head, unsigned int sector, const void* buffer);

#endif // FDD_H
