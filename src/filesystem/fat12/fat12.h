#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stdbool.h>

#include "toolchain/strings.h"
#include "toolchain/stdlib.h"
#include "drivers/fdd/fdd.h"

#define SECTOR_SIZE 512

typedef struct {
    uint8_t  jmp_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat12_bpb_t;

typedef struct {
    fat12_bpb_t bpb;         // BIOS Parameter Block
    uint16_t fat_start;      // Start sector of the first FAT
    uint16_t root_dir_start; // Start sector of the root directory
    uint16_t data_start;     // Start sector of data area
} fat12_fs_t;


bool fat12_init_fs();
int fat12_read_dir(const char* path, char* buffer, unsigned int* size);

#endif // FAT12_H
