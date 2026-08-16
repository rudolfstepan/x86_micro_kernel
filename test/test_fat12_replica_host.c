#include <string.h>

#include "fs/fat12/fat12_replica.h"

static unsigned char disk[32][FAT12_REPLICA_SECTOR_SIZE];

static bool read_sector(void *context, uint32_t sector, void *buffer) {
    (void)context;
    if (sector >= 32U) return false;
    memcpy(buffer, disk[sector], FAT12_REPLICA_SECTOR_SIZE);
    return true;
}

static bool write_sector(void *context, uint32_t sector, const void *buffer) {
    (void)context;
    if (sector >= 32U) return false;
    memcpy(disk[sector], buffer, FAT12_REPLICA_SECTOR_SIZE);
    return true;
}

int main(void) {
    fat12_replica_t replica;
    unsigned char source[] = {1, 2, 3, 4};
    unsigned char output[8] = {0};
    size_t length = 0U;
    if (!fat12_replica_init(&replica, 2U, 11U, 0x12345678U) ||
        !fat12_replica_publish(&replica, source, sizeof(source), 1U))
        return 1;
    replica.primary_data[0] ^= 1U;
    if (!fat12_replica_select(&replica, output, sizeof(output), &length) ||
        length != sizeof(source) || memcmp(output, source, sizeof(source)) != 0)
        return 2;
    if (!fat12_replica_publish(&replica, source, sizeof(source), 2U)) return 3;
    replica.mirror_data[0] ^= 1U;
    replica.mirror_header.crc32 = fat12_replica_crc32(replica.mirror_data,
                                                       sizeof(source));
    if (fat12_replica_select(&replica, output, sizeof(output), &length)) return 4;
    if (!fat12_replica_publish_persistent(&replica, source, sizeof(source), 3U,
            read_sector, write_sector, NULL)) return 5;
    fat12_replica_t loaded;
    if (!fat12_replica_init(&loaded, 2U, 11U, 0x12345678U) ||
        !fat12_replica_load(&loaded, read_sector, NULL) ||
        !fat12_replica_select(&loaded, output, sizeof(output), &length) ||
        length != sizeof(source) || memcmp(output, source, sizeof(source)) != 0)
        return 6;
    disk[3][0] ^= 1U;
    if (!fat12_replica_load(&loaded, read_sector, NULL) ||
        !fat12_replica_select(&loaded, output, sizeof(output), &length))
        return 7;
    return 0;
}
