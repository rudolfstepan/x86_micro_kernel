/**
 * @file fs/fat12/fat12_critical.h
 * @brief Schutzvertrag kritischer FAT12-Metadaten.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Redundante Kopien werden versions- und CRC-geprüft.
 */
#ifndef FAT12_CRITICAL_H
#define FAT12_CRITICAL_H

#include <stdbool.h>

/* Explicit allow-list.  New names require a deliberate format/layout change. */
bool fat12_is_critical_name(const char *name);

#endif
