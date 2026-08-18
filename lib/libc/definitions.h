/**
 * @file lib/libc/definitions.h
 * @brief Gemeinsame freestanding Typ- und Makrodefinitionen.
 *
 * Layer: Freestanding kernel/userspace runtime.
 * Contract: Größen, Versionen und Pufferbereiche werden vor Lesen, Schreiben oder Syscall geprüft.
 * Safety: Parser und Formatierung sind kapazitätsbegrenzt; Fehler erzeugen keine partiellen Ausgaben.
 */
#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#include <stddef.h>

typedef struct {
    unsigned char *base;  // Base address of the file in memory
    unsigned char *ptr;   // Current read/write position
    unsigned int start_cluster;    // start_cluster of the file
    char mode[4];         // Stable copy of the open mode
    char name[13];        // Stable 8.3 filename copy
    unsigned int parent_cluster; // Directory containing the file entry
    unsigned int partition_lba;  // FAT32 volume identity for stable handles
    unsigned short device_base;
    unsigned char device_master;
    size_t size;          // Size of the file
    size_t position;      // Current position in the file (offset from base)
} FILE;

#endif // DEFINITIONS_H
