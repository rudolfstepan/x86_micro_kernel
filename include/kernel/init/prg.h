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

#define EI_NIDENT 16
#define PT_LOAD 1 // Loadable segment type in ELF


typedef struct {
    unsigned char e_ident[EI_NIDENT]; // ELF Identification
    uint16_t e_type;                  // Object file type
    uint16_t e_machine;               // Machine type
    uint32_t e_version;               // Object file version
    uint32_t e_entry;                 // Entry point address
    uint32_t e_phoff;                 // Program header offset
    uint32_t e_shoff;                 // Section header offset
    uint32_t e_flags;                 // Processor-specific flags
    uint16_t e_ehsize;                // ELF header size
    uint16_t e_phentsize;             // Program header entry size
    uint16_t e_phnum;                 // Number of program header entries
    uint16_t e_shentsize;             // Section header entry size
    uint16_t e_shnum;                 // Number of section header entries
    uint16_t e_shstrndx;              // Section header string table index
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;   // Type of segment
    uint32_t p_offset; // Offset in file
    uint32_t p_vaddr;  // Virtual address in memory
    uint32_t p_paddr;  // Physical address (unused)
    uint32_t p_filesz; // Size of segment in file
    uint32_t p_memsz;  // Size of segment in memory
    uint32_t p_flags;  // Segment flags
    uint32_t p_align;  // Segment alignment
} Elf32_Phdr;

extern uint32_t _text_start, _text_end;
extern uint32_t _relocation_offset, _relocation_end;

void apply_relocation(uint32_t* relocation_table, uint32_t relocation_count, uint32_t offset);
void load_and_relocate_program(void* program_src, void* target_address);
int load_elf(void *elf_data);

#endif // PRG_H