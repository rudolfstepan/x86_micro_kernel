/**
 * @file fs/fat12/fat12_critical.c
 * @brief Validierung und Recovery kritischer FAT12-Sektoren.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Doppelkorruption führt read-only statt zu heuristischer Reparatur.
 */
#include "fat12_critical.h"

#include "lib/libc/string.h"

bool fat12_is_critical_name(const char *name) {
    if (name == NULL) return false;
    return (strlen(name) == 9U && strncasecmp(name, "REIST.CFG", 9U) == 0) ||
           (strlen(name) == 11U && strncasecmp(name, "STORAGE.CFG", 11U) == 0) ||
           (strlen(name) == 8U && strncasecmp(name, "BOOT.CFG", 8U) == 0);
}
