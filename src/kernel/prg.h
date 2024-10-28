// the prg file format which is used to store the compiled program header

#ifndef PRG_H
#define PRG_H

#include <stdint.h>


// The program header is a 12-byte structure that is stored at the beginning of a binary, executable file.
#pragma pack(push, 1)
typedef struct {
    char identifier[4];  // 4-byte string identifier, e.g., "MYPR"
    uint16_t version;    // Version number
    uint32_t entryPoint; // Entry point of the program
    uint32_t programSize;// Size of the code segment
} ProgramHeader;
#pragma pack(pop)

#endif // PRG_H