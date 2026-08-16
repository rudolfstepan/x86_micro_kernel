#ifndef FAT12_REMAP_H
#define FAT12_REMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAT12_REMAP_MAX_ENTRIES 16U
#define FAT12_REMAP_VERSION 1U
#define FAT12_REMAP_MAGIC 0x52504D31U /* RPM1 */
#define FAT12_REMAP_SECTOR_SIZE 512U
#define FAT12_REMAP_SPARE_COUNT 8U

typedef bool (*fat12_remap_read_fn)(void *context, uint32_t sector,
                                    void *buffer);
typedef bool (*fat12_remap_write_fn)(void *context, uint32_t sector,
                                     const void *buffer);

typedef struct __attribute__((packed)) {
    uint32_t bad_sector;
    uint32_t replacement_sector;
    uint64_t sequence;
} fat12_remap_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t entry_size;
    uint32_t media_fingerprint;
    uint64_t sequence;
    uint32_t entry_count;
    uint32_t crc32;
} fat12_remap_header_t;

typedef struct {
    uint32_t primary_sector;
    uint32_t mirror_sector;
    uint32_t table_sector;
    uint32_t media_fingerprint;
    fat12_remap_header_t header;
    fat12_remap_entry_t entries[FAT12_REMAP_MAX_ENTRIES];
} fat12_remap_table_t;

uint32_t fat12_remap_crc32(const void *data, size_t length);
bool fat12_remap_format(fat12_remap_table_t *table, uint32_t primary_sector,
                        uint32_t mirror_sector, uint32_t table_sector,
                        uint32_t media_fingerprint);
bool fat12_remap_load(fat12_remap_table_t *table, fat12_remap_read_fn read,
                      void *context);
bool fat12_remap_add(fat12_remap_table_t *table, uint32_t bad_sector,
                     uint32_t replacement_sector, fat12_remap_read_fn read,
                     fat12_remap_write_fn write, void *context);
bool fat12_remap_lookup(const fat12_remap_table_t *table, uint32_t bad_sector,
                        uint32_t *replacement_sector);

#endif
