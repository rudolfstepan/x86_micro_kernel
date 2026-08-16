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
        primary_sector > UINT32_MAX - FAT12_REPLICA_DATA_SECTORS ||
        mirror_sector <= primary_sector + FAT12_REPLICA_DATA_SECTORS ||
        mirror_sector > UINT32_MAX - FAT12_REPLICA_DATA_SECTORS) return false;
    memset(replica, 0, sizeof(*replica));
    replica->primary_sector = primary_sector;
    replica->mirror_sector = mirror_sector;
    replica->media_fingerprint = media_fingerprint;
    return true;
}

static bool write_verified(fat12_replica_read_fn read,
        fat12_replica_write_fn write, void *context, uint32_t sector,
        const void *data) {
    uint8_t verify[FAT12_REPLICA_SECTOR_SIZE];
    return read != NULL && write != NULL && write(context, sector, data) &&
           read(context, sector, verify) &&
           memcmp(data, verify, sizeof(verify)) == 0;
}

static bool write_copy(uint32_t header_sector,
        const fat12_replica_header_t *header, const uint8_t *data,
        fat12_replica_read_fn read, fat12_replica_write_fn write,
        void *context) {
    uint8_t sector[FAT12_REPLICA_SECTOR_SIZE];
    for (uint32_t index = 0U; index < FAT12_REPLICA_DATA_SECTORS; ++index) {
        memset(sector, 0, sizeof(sector));
        size_t offset = (size_t)index * sizeof(sector);
        size_t remaining = header->length > offset ? header->length - offset : 0U;
        size_t amount = remaining < sizeof(sector) ? remaining : sizeof(sector);
        if (amount != 0U) memcpy(sector, data + offset, amount);
        if (!write_verified(read, write, context, header_sector + 1U + index,
                            sector)) return false;
    }
    memset(sector, 0, sizeof(sector));
    memcpy(sector, header, sizeof(*header));
    return write_verified(read, write, context, header_sector, sector);
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

bool fat12_replica_publish_persistent(fat12_replica_t *replica,
        const void *data, size_t length, uint64_t sequence,
        fat12_replica_read_fn read, fat12_replica_write_fn write,
        void *context) {
    if (!fat12_replica_publish(replica, data, length, sequence) ||
        read == NULL || write == NULL) return false;
    if (!write_copy(replica->primary_sector, &replica->primary_header,
                    replica->primary_data, read, write, context)) return false;
    return write_copy(replica->mirror_sector, &replica->mirror_header,
                      replica->mirror_data, read, write, context);
}

static bool load_copy(uint32_t header_sector, uint32_t fingerprint,
        fat12_replica_header_t *header, uint8_t *data,
        fat12_replica_read_fn read, void *context) {
    uint8_t sector[FAT12_REPLICA_SECTOR_SIZE];
    if (!read(context, header_sector, sector)) return false;
    memcpy(header, sector, sizeof(*header));
    if (header->magic != FAT12_REPLICA_MAGIC ||
        header->version != FAT12_REPLICA_VERSION ||
        header->header_size != sizeof(*header) ||
        header->media_fingerprint != fingerprint || header->sequence == 0U ||
        header->length == 0U || header->length > FAT12_REPLICA_MAX_BYTES)
        return false;
    memset(data, 0, FAT12_REPLICA_MAX_BYTES);
    for (uint32_t index = 0U; index < FAT12_REPLICA_DATA_SECTORS; ++index) {
        if (!read(context, header_sector + 1U + index, sector)) return false;
        size_t offset = (size_t)index * sizeof(sector);
        size_t remaining = header->length > offset ? header->length - offset : 0U;
        size_t amount = remaining < sizeof(sector) ? remaining : sizeof(sector);
        if (amount != 0U) memcpy(data + offset, sector, amount);
    }
    return valid_header(header, data, fingerprint);
}

bool fat12_replica_load(fat12_replica_t *replica,
                        fat12_replica_read_fn read, void *context) {
    if (replica == NULL || read == NULL || replica->media_fingerprint == 0U)
        return false;
    bool primary_ok = load_copy(replica->primary_sector,
        replica->media_fingerprint, &replica->primary_header,
        replica->primary_data, read, context);
    bool mirror_ok = load_copy(replica->mirror_sector,
        replica->media_fingerprint, &replica->mirror_header,
        replica->mirror_data, read, context);
    if (!primary_ok) memset(&replica->primary_header, 0,
                            sizeof(replica->primary_header));
    if (!mirror_ok) memset(&replica->mirror_header, 0,
                           sizeof(replica->mirror_header));
    return primary_ok || mirror_ok;
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
