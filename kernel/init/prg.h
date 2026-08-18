/**
 * @file kernel/init/prg.h
 * @brief PRG-Loader-Vertrag.
 *
 * Layer: Ring-0 kernel interface.
 * Contract: Validiert Images vollständig vor Prozesspublikation.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#ifndef PRG_H
#define PRG_H

#include <stdint.h>

#define PROGRAM_IMAGE_MAGIC 0xDEADBEEFU
#define PROGRAM_V1_BASE 0x40000000U
#define PROGRAM_V1_REGION_SIZE (8U * 1024U * 1024U)

/* Fixed-address MYPR v1 header.  Version 1 has no version field; this exact
 * legacy layout is the contract and unsupported variants are rejected. */
#pragma pack(push, 1)
typedef struct {
    char identifier[4];         /* ASCII "MYPR" */
    uint32_t magic_number;      /* PROGRAM_IMAGE_MAGIC */
    uint32_t entry_point;       /* Offset relative to base_address */
    uint32_t program_size;      /* Memory payload after this header */
    uint32_t base_address;      /* PROGRAM_V1_BASE */
    uint32_t relocation_offset; /* Stored v1 image size / end of file */
    uint32_t relocation_size;   /* Reserved; must be zero in v1 */
} program_header_t;
#pragma pack(pop)

_Static_assert(sizeof(program_header_t) == 28,
               "MYPR v1 header must remain 28 bytes");

#endif // PRG_H
