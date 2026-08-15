#ifndef FAT12_REPLICA_H
#define FAT12_REPLICA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAT12_REPLICA_MAX_BYTES 4096U
#define FAT12_REPLICA_MAGIC 0x52504C31U /* RPL1 */
#define FAT12_REPLICA_VERSION 1U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t media_fingerprint;
    uint64_t sequence;
    uint32_t length;
    uint32_t crc32;
} fat12_replica_header_t;

typedef struct {
    uint32_t primary_sector;
    uint32_t mirror_sector;
    uint32_t media_fingerprint;
    fat12_replica_header_t primary_header;
    fat12_replica_header_t mirror_header;
    uint8_t primary_data[FAT12_REPLICA_MAX_BYTES];
    uint8_t mirror_data[FAT12_REPLICA_MAX_BYTES];
} fat12_replica_t;

uint32_t fat12_replica_crc32(const void *data, size_t length);
bool fat12_replica_init(fat12_replica_t *replica, uint32_t primary_sector,
                        uint32_t mirror_sector, uint32_t media_fingerprint);
bool fat12_replica_publish(fat12_replica_t *replica, const void *data,
                           size_t length, uint64_t sequence);
bool fat12_replica_select(const fat12_replica_t *replica, void *output,
                          size_t capacity, size_t *length_out);

#endif
