#include <string.h>

#include "fs/fat12/fat12_replica.h"

int main(void) {
    fat12_replica_t replica;
    unsigned char source[] = {1, 2, 3, 4};
    unsigned char output[8] = {0};
    size_t length = 0U;
    if (!fat12_replica_init(&replica, 30U, 31U, 0x12345678U) ||
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
    return 0;
}
