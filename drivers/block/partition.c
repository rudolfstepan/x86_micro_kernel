#include "partition.h"

#include "block_device.h"
#include "drivers/block/ata.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"

#define MBR_SIGNATURE_OFFSET 510U
#define MBR_ENTRY_OFFSET 446U
#define MBR_ENTRY_SIZE 16U
#define MBR_PRIMARY_COUNT 4U
#define GPT_HEADER_MIN_SIZE 92U
#define GPT_ENTRY_SIZE 128U
#define GPT_MAX_ENTRIES 128U
#define GPT_MAX_CHILDREN MAX_PARTITION_DRIVES
#define PARTITION_VIRTUAL_BASE 0xB000U

typedef struct {
    uint32_t first_lba, sectors;
    uint8_t type, index, scheme;
    uint8_t type_guid[16], guid[16];
} partition_candidate_t;

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4U) << 32U);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

static bool guid_is_zero(const uint8_t guid[16]) {
    uint8_t value = 0U;
    for (size_t i = 0U; i < 16U; ++i) value |= guid[i];
    return value == 0U;
}

static bool candidates_valid(const partition_candidate_t *items, size_t count) {
    for (size_t a = 0U; a < count; ++a) {
        uint64_t a_end = (uint64_t)items[a].first_lba + items[a].sectors;
        for (size_t b = a + 1U; b < count; ++b) {
            uint64_t b_end = (uint64_t)items[b].first_lba + items[b].sectors;
            if (((uint64_t)items[a].first_lba < b_end &&
                 (uint64_t)items[b].first_lba < a_end) ||
                (items[a].scheme == PARTITION_SCHEME_GPT &&
                 memcmp(items[a].guid, items[b].guid, 16U) == 0)) return false;
        }
    }
    return true;
}

static size_t publish(uint8_t parent_id, const partition_candidate_t *items,
                      size_t count) {
    if (count == 0U || (size_t)drive_count + count > MAX_DRIVES) return 0U;
    drive_t *parent = &detected_drives[parent_id];
    for (size_t i = 0U; i < count; ++i) {
        drive_t *child = &detected_drives[drive_count];
        memset(child, 0, sizeof(*child));
        child->type = DRIVE_TYPE_PARTITION;
        child->base = (uint16_t)(PARTITION_VIRTUAL_BASE | (uint16_t)drive_count);
        child->sectors = items[i].sectors;
        child->parent_resource = parent_id;
        child->partition_type = items[i].type;
        child->partition_index = items[i].index;
        child->partition_scheme = items[i].scheme;
        child->lba_offset = items[i].first_lba;
        memcpy(child->partition_type_guid, items[i].type_guid, 16U);
        memcpy(child->partition_guid, items[i].guid, 16U);
        snprintf(child->name, sizeof(child->name), "%sp%u", parent->name,
                 (unsigned)items[i].index);
        if (items[i].scheme == PARTITION_SCHEME_GPT)
            snprintf(child->model, sizeof(child->model), "GPT entry %u on %s",
                     (unsigned)items[i].index, parent->name);
        else
            snprintf(child->model, sizeof(child->model), "MBR type %02X on %s",
                     items[i].type, parent->name);
        ++drive_count;
    }
    parent->has_partitions = true;
    return count;
}

static size_t discover_gpt(uint8_t parent_id) {
    drive_t *parent = &detected_drives[parent_id];
    uint8_t header[512];
    if (parent->sectors < 3U ||
        block_device_read_sector(parent, 1U, header) != BLOCK_DEVICE_OK ||
        memcmp(header, "EFI PART", 8U) != 0) return 0U;
    uint32_t header_size = read_le32(header + 12U);
    uint32_t header_crc = read_le32(header + 16U);
    uint64_t first_usable = read_le64(header + 40U);
    uint64_t last_usable = read_le64(header + 48U);
    uint64_t entries_lba = read_le64(header + 72U);
    uint32_t entry_count = read_le32(header + 80U);
    uint32_t entry_size = read_le32(header + 84U);
    uint32_t entries_expected_crc = read_le32(header + 88U);
    if (header_size < GPT_HEADER_MIN_SIZE || header_size > sizeof(header) ||
        read_le32(header + 8U) != 0x00010000U ||
        read_le32(header + 20U) != 0U || read_le64(header + 24U) != 1U ||
        read_le64(header + 32U) != (uint64_t)parent->sectors - 1U ||
        guid_is_zero(header + 56U) ||
        first_usable > last_usable || last_usable >= parent->sectors ||
        entries_lba < 2U || entries_lba >= parent->sectors ||
        entry_count == 0U || entry_count > GPT_MAX_ENTRIES ||
        entry_size != GPT_ENTRY_SIZE) return 0U;
    memset(header + 16U, 0, 4U);
    if ((crc32_update(0xFFFFFFFFU, header, header_size) ^ 0xFFFFFFFFU) !=
        header_crc) return 0U;

    uint64_t bytes_left = (uint64_t)entry_count * entry_size;
    uint64_t table_sectors = (bytes_left + 511U) / 512U;
    if (entries_lba + table_sectors > first_usable) return 0U;
    partition_candidate_t items[GPT_MAX_CHILDREN];
    size_t count = 0U;
    uint32_t entries_crc = 0xFFFFFFFFU, seen = 0U;
    uint8_t sector[512];
    for (uint64_t s = 0U; s < table_sectors; ++s) {
        if (block_device_read_sector(parent, (uint32_t)(entries_lba + s),
                                     sector) != BLOCK_DEVICE_OK) return 0U;
        size_t used = bytes_left >= 512U ? 512U : (size_t)bytes_left;
        entries_crc = crc32_update(entries_crc, sector, used);
        bytes_left -= used;
        for (size_t offset = 0U; offset < used && seen < entry_count;
             offset += GPT_ENTRY_SIZE, ++seen) {
            const uint8_t *entry = sector + offset;
            if (guid_is_zero(entry)) continue;
            uint64_t first = read_le64(entry + 32U), last = read_le64(entry + 40U);
            if (guid_is_zero(entry + 16U) || first < first_usable ||
                last > last_usable || first > last || first > UINT32_MAX ||
                last - first + 1U > UINT32_MAX || count >= GPT_MAX_CHILDREN)
                return 0U;
            partition_candidate_t *item = &items[count++];
            memset(item, 0, sizeof(*item));
            item->first_lba = (uint32_t)first;
            item->sectors = (uint32_t)(last - first + 1U);
            item->index = (uint8_t)seen + 1U;
            item->scheme = PARTITION_SCHEME_GPT;
            memcpy(item->type_guid, entry, 16U);
            memcpy(item->guid, entry + 16U, 16U);
        }
    }
    if ((entries_crc ^ 0xFFFFFFFFU) != entries_expected_crc ||
        !candidates_valid(items, count)) return 0U;
    return publish(parent_id, items, count);
}

static size_t discover_parent(uint8_t parent_id) {
    drive_t *parent = &detected_drives[parent_id];
    uint8_t sector[512];
    if ((parent->type != DRIVE_TYPE_ATA && parent->type != DRIVE_TYPE_AHCI) ||
        parent->sectors == 0U ||
        block_device_read_sector(parent, 0U, sector) != BLOCK_DEVICE_OK ||
        sector[MBR_SIGNATURE_OFFSET] != 0x55U ||
        sector[MBR_SIGNATURE_OFFSET + 1U] != 0xAAU) return 0U;
    partition_candidate_t items[MBR_PRIMARY_COUNT];
    size_t count = 0U;
    bool protective = false;
    for (uint8_t slot = 0U; slot < MBR_PRIMARY_COUNT; ++slot) {
        const uint8_t *entry = sector + MBR_ENTRY_OFFSET + slot * MBR_ENTRY_SIZE;
        uint8_t boot = entry[0], type = entry[4];
        uint32_t first = read_le32(entry + 8U), sectors = read_le32(entry + 12U);
        if (type == 0U && first == 0U && sectors == 0U) continue;
        if (type == 0xEEU) {
            if (protective || boot != 0U || first != 1U || sectors == 0U ||
                (sectors != UINT32_MAX && sectors < parent->sectors - 1U))
                return 0U;
            protective = true;
            continue;
        }
        if (protective || (boot != 0U && boot != 0x80U) || type == 0U ||
            first == 0U || sectors == 0U ||
            (uint64_t)first + sectors > parent->sectors) return 0U;
        partition_candidate_t *item = &items[count++];
        memset(item, 0, sizeof(*item));
        item->first_lba = first;
        item->sectors = sectors;
        item->type = type;
        item->index = (uint8_t)(slot + 1U);
        item->scheme = PARTITION_SCHEME_MBR;
    }
    if (protective) return count == 0U ? discover_gpt(parent_id) : 0U;
    if (!candidates_valid(items, count)) return 0U;
    return publish(parent_id, items, count);
}

size_t partition_discover(void) {
    if (drive_count < 0 || drive_count > MAX_DRIVES) return 0U;
    uint8_t physical_count = (uint8_t)drive_count;
    size_t published = 0U;
    for (uint8_t resource = 0U; resource < physical_count; ++resource)
        published += discover_parent(resource);
    if (published != 0U)
        printf("PART: published %u bounded partition device(s)\n",
               (unsigned)published);
    return published;
}
