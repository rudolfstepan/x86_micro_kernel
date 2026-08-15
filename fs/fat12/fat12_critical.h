#ifndef FAT12_CRITICAL_H
#define FAT12_CRITICAL_H

#include <stdbool.h>

/* Explicit allow-list.  New names require a deliberate format/layout change. */
bool fat12_is_critical_name(const char *name);

#endif
