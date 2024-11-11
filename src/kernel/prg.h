// the prg file format which is used to store the compiled program header

#ifndef PRG_H
#define PRG_H

#include <stdint.h>


// The program header is a 12-byte structure that is stored at the beginning of a binary, executable file.
#pragma pack(push, 1)
typedef struct {
    char identifier[4];         // 4-byte string identifier, e.g., "MYPR"
    uint32_t magic_number;      // Zur Validierung des Programms
    uint32_t entry_point;       // Adresse des Programmstarts
    uint32_t program_size;      // Größe des Programms in Bytes
    uint32_t base_address;      // Ursprüngliche Basisadresse
    uint32_t relocation_offset; // Offset zur Relocation-Tabelle
    uint32_t relocation_size;   // Größe der Relocation-Tabelle
    // Weitere Felder nach Bedarf
} program_header_t;

#pragma pack(pop)

#endif // PRG_H