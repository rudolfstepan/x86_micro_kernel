#include "partition.h"

#include "block_device.h"
#include "drivers/block/ata.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#define MBR_SIGNATURE_OFFSET 510U
#define MBR_ENTRY_OFFSET 446U
#define MBR_ENTRY_SIZE 16U
#define MBR_PRIMARY_COUNT 4U
#define PARTITION_VIRTUAL_BASE 0xB000U

typedef struct {
    uint32_t first_lba;
    uint32_t sectors;
    uint8_t type;
    uint8_t slot;
} mbr_candidate_t;

static uint32_t read_le32(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}

static bool candidates_overlap(const mbr_candidate_t *left,
                               const mbr_candidate_t *right) {
    uint64_t left_end = (uint64_t)left->first_lba + left->sectors;
    uint64_t right_end = (uint64_t)right->first_lba + right->sectors;
    return (uint64_t)left->first_lba < right_end &&
           (uint64_t)right->first_lba < left_end;
}

static size_t discover_on_parent(uint8_t parent_resource) {
    drive_t *parent = &detected_drives[parent_resource];
    uint8_t sector[512];
    if ((parent->type != DRIVE_TYPE_ATA &&
         parent->type != DRIVE_TYPE_AHCI) || parent->sectors == 0U ||
        block_device_read_sector(parent, 0U, sector) != BLOCK_DEVICE_OK ||
        sector[MBR_SIGNATURE_OFFSET] != 0x55U ||
        sector[MBR_SIGNATURE_OFFSET + 1U] != 0xAAU) return 0U;

    mbr_candidate_t candidates[MBR_PRIMARY_COUNT];
    size_t candidate_count = 0U;
    for (uint8_t slot = 0U; slot < MBR_PRIMARY_COUNT; ++slot) {
        const uint8_t *entry = sector + MBR_ENTRY_OFFSET +
                               (size_t)slot * MBR_ENTRY_SIZE;
        uint8_t boot = entry[0];
        uint8_t type = entry[4];
        uint32_t first = read_le32(entry + 8U);
        uint32_t count = read_le32(entry + 12U);
        if (type == 0U && first == 0U && count == 0U) continue;
        if ((boot != 0U && boot != 0x80U) || type == 0U || type == 0xEEU ||
            first == 0U || count == 0U ||
            (uint64_t)first + count > parent->sectors) return 0U;
        candidates[candidate_count++] = (mbr_candidate_t){
            .first_lba = first, .sectors = count,
            .type = type, .slot = (uint8_t)(slot + 1U),
        };
    }
    for (size_t left = 0U; left < candidate_count; ++left) {
        for (size_t right = left + 1U; right < candidate_count; ++right) {
            if (candidates_overlap(&candidates[left], &candidates[right]))
                return 0U;
        }
    }
    if (candidate_count == 0U ||
        (size_t)drive_count + candidate_count > MAX_DRIVES) return 0U;

    for (size_t index = 0U; index < candidate_count; ++index) {
        drive_t *child = &detected_drives[drive_count];
        memset(child, 0, sizeof(*child));
        child->type = DRIVE_TYPE_PARTITION;
        child->base = (uint16_t)(PARTITION_VIRTUAL_BASE |
                                 (uint16_t)drive_count);
        child->sectors = candidates[index].sectors;
        child->parent_resource = parent_resource;
        child->partition_type = candidates[index].type;
        child->partition_index = candidates[index].slot;
        child->lba_offset = candidates[index].first_lba;
        snprintf(child->name, sizeof(child->name), "%sp%u", parent->name,
                 (unsigned)candidates[index].slot);
        snprintf(child->model, sizeof(child->model), "MBR type %02X on %s",
                 candidates[index].type, parent->name);
        ++drive_count;
    }
    parent->has_partitions = true;
    return candidate_count;
}

size_t partition_discover_mbr(void) {
    if (drive_count < 0 || drive_count > MAX_DRIVES) return 0U;
    uint8_t physical_count = (uint8_t)drive_count;
    size_t published = 0U;
    for (uint8_t resource = 0U; resource < physical_count; ++resource)
        published += discover_on_parent(resource);
    if (published != 0U)
        printf("MBR: published %u bounded partition device(s)\n",
               (unsigned)published);
    return published;
}
