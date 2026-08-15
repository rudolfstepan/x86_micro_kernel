#include "fat12_replica.h"

#include "lib/libc/string.h"

static bool valid_header(const fat12_replica_header_t *header,
                         const uint8_t *data, uint32_t fingerprint) {
    if (header == NULL || data == NULL || header->magic != FAT12_REPLICA_MAGIC ||
        header->version != FAT12_REPLICA_VERSION ||
        header->header_size != sizeof(*header) ||
        header->media_fingerprint != fingerprint || header->sequence == 0U ||
        header->length > FAT12_REPLICA_MAX_BYTES) return false;
    return header->crc32 == fat12_replica_crc32(data, header->length);
}

uint32_t fat12_replica_crc32(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    if (bytes == NULL && length != 0U) return 0U;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U &
                  (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFU;
}

bool fat12_replica_init(fat12_replica_t *replica, uint32_t primary_sector,
        uint32_t mirror_sector, uint32_t media_fingerprint) {
    if (replica == NULL || media_fingerprint == 0U ||
        primary_sector == mirror_sector) return false;
    memset(replica, 0, sizeof(*replica));
    replica->primary_sector = primary_sector;
    replica->mirror_sector = mirror_sector;
    replica->media_fingerprint = media_fingerprint;
    return true;
}

bool fat12_replica_publish(fat12_replica_t *replica, const void *data,
                           size_t length, uint64_t sequence) {
    if (replica == NULL || data == NULL || length == 0U ||
        length > FAT12_REPLICA_MAX_BYTES || sequence == 0U) return false;
    fat12_replica_header_t header = {
        .magic = FAT12_REPLICA_MAGIC,
        .version = FAT12_REPLICA_VERSION,
        .header_size = sizeof(header),
        .media_fingerprint = replica->media_fingerprint,
        .sequence = sequence,
        .length = (uint32_t)length,
        .crc32 = fat12_replica_crc32(data, length),
    };
    memcpy(replica->primary_data, data, length);
    memcpy(replica->mirror_data, data, length);
    replica->primary_header = header;
    replica->mirror_header = header;
    return true;
}

bool fat12_replica_select(const fat12_replica_t *replica, void *output,
        size_t capacity, size_t *length_out) {
    if (replica == NULL || output == NULL || length_out == NULL) return false;
    bool primary_ok = valid_header(&replica->primary_header,
                                   replica->primary_data,
                                   replica->media_fingerprint);
    bool mirror_ok = valid_header(&replica->mirror_header,
                                  replica->mirror_data,
                                  replica->media_fingerprint);
    if (!primary_ok && !mirror_ok) return false;
    const fat12_replica_header_t *selected = NULL;
    const uint8_t *data = NULL;
    if (primary_ok && mirror_ok) {
        if (replica->primary_header.sequence == replica->mirror_header.sequence) {
            if (replica->primary_header.length != replica->mirror_header.length ||
                memcmp(replica->primary_data, replica->mirror_data,
                       replica->primary_header.length) != 0) return false;
            selected = &replica->primary_header;
            data = replica->primary_data;
        } else if (replica->primary_header.sequence >
                   replica->mirror_header.sequence) {
            selected = &replica->primary_header; data = replica->primary_data;
        } else { selected = &replica->mirror_header; data = replica->mirror_data; }
    } else if (primary_ok) {
        selected = &replica->primary_header; data = replica->primary_data;
    } else { selected = &replica->mirror_header; data = replica->mirror_data; }
    if (capacity < selected->length) return false;
    memcpy(output, data, selected->length);
    *length_out = selected->length;
    return true;
}
