#include "fat12_remap.h"

#include "lib/libc/string.h"

static bool header_shape_valid(const fat12_remap_header_t *header,
                               uint32_t fingerprint) {
    if (header == NULL || header->magic != FAT12_REMAP_MAGIC ||
        header->version != FAT12_REMAP_VERSION ||
        header->entry_size != sizeof(fat12_remap_entry_t) ||
        header->media_fingerprint != fingerprint ||
        header->entry_count > FAT12_REMAP_MAX_ENTRIES) return false;
    return true;
}

static uint32_t table_crc(const fat12_remap_header_t *header,
                          const fat12_remap_entry_t *entries) {
    fat12_remap_header_t copy = *header;
    copy.crc32 = 0U;
    uint8_t payload[sizeof(copy) +
                    FAT12_REMAP_MAX_ENTRIES * sizeof(*entries)];
    memcpy(payload, &copy, sizeof(copy));
    size_t entry_bytes = copy.entry_count * sizeof(*entries);
    if (entry_bytes != 0U)
        memcpy(payload + sizeof(copy), entries, entry_bytes);
    return fat12_remap_crc32(payload, sizeof(copy) + entry_bytes);
}

static bool entries_valid(const fat12_remap_header_t *header,
                          const fat12_remap_entry_t *entries) {
    for (uint32_t i = 0U; i < header->entry_count; ++i) {
        if (entries[i].bad_sector == 0U ||
            entries[i].replacement_sector == 0U ||
            entries[i].bad_sector == entries[i].replacement_sector ||
            entries[i].sequence == 0U ||
            entries[i].sequence > header->sequence) return false;
        for (uint32_t j = 0U; j < i; ++j)
            if (entries[j].bad_sector == entries[i].bad_sector ||
                entries[j].replacement_sector == entries[i].replacement_sector ||
                entries[j].bad_sector == entries[i].replacement_sector ||
                entries[j].replacement_sector == entries[i].bad_sector)
                return false;
    }
    return header->crc32 == table_crc(header, entries);
}

static void prepare_header(fat12_remap_header_t *header, uint32_t fingerprint,
                           uint64_t sequence, uint32_t count) {
    *header = (fat12_remap_header_t){
        .magic = FAT12_REMAP_MAGIC,
        .version = FAT12_REMAP_VERSION,
        .entry_size = sizeof(fat12_remap_entry_t),
        .media_fingerprint = fingerprint,
        .sequence = sequence,
        .entry_count = count,
        .crc32 = 0U,
    };
    header->crc32 = 0U;
}

uint32_t fat12_remap_crc32(const void *data, size_t length) {
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

bool fat12_remap_format(fat12_remap_table_t *table, uint32_t primary_sector,
        uint32_t mirror_sector, uint32_t table_sector,
        uint32_t media_fingerprint) {
    if (table == NULL || media_fingerprint == 0U ||
        primary_sector == mirror_sector || table_sector <= mirror_sector)
        return false;
    memset(table, 0, sizeof(*table));
    table->primary_sector = primary_sector;
    table->mirror_sector = mirror_sector;
    table->table_sector = table_sector;
    table->media_fingerprint = media_fingerprint;
    prepare_header(&table->header, media_fingerprint, 1U, 0U);
    table->header.crc32 = table_crc(&table->header, table->entries);
    return true;
}

bool fat12_remap_load(fat12_remap_table_t *table, fat12_remap_read_fn read,
                      void *context) {
    if (table == NULL || read == NULL || table->media_fingerprint == 0U)
        return false;
    uint8_t primary[FAT12_REMAP_SECTOR_SIZE];
    uint8_t mirror[FAT12_REMAP_SECTOR_SIZE];
    uint8_t entries_sector[FAT12_REMAP_SECTOR_SIZE];
    fat12_remap_header_t first, second;
    bool first_ok = read(context, table->primary_sector, primary) &&
        (memcpy(&first, primary, sizeof(first)),
         header_shape_valid(&first, table->media_fingerprint));
    bool second_ok = read(context, table->mirror_sector, mirror) &&
        (memcpy(&second, mirror, sizeof(second)),
         header_shape_valid(&second, table->media_fingerprint));
    if (!first_ok && !second_ok) return false;
    memset(entries_sector, 0, sizeof(entries_sector));
    uint32_t maximum_count = first_ok ? first.entry_count : 0U;
    if (second_ok && second.entry_count > maximum_count)
        maximum_count = second.entry_count;
    if (maximum_count != 0U &&
        !read(context, table->table_sector, entries_sector)) return false;
    memcpy(table->entries, entries_sector, sizeof(table->entries));
    first_ok = first_ok && entries_valid(&first, table->entries);
    second_ok = second_ok && entries_valid(&second, table->entries);
    if (!first_ok && !second_ok) return false;
    if (first_ok && second_ok && first.sequence == second.sequence &&
        memcmp(&first, &second, sizeof(first)) != 0) return false;
    table->header = !second_ok || (first_ok && first.sequence >= second.sequence)
        ? first : second;
    memset(table->entries + table->header.entry_count, 0,
           sizeof(table->entries) -
           table->header.entry_count * sizeof(table->entries[0]));
    return true;
}

static bool write_verified(fat12_remap_read_fn read,
        fat12_remap_write_fn write, void *context, uint32_t sector,
        const void *data) {
    uint8_t verify[FAT12_REMAP_SECTOR_SIZE];
    return read != NULL && write != NULL && write(context, sector, data) &&
           read(context, sector, verify) &&
           memcmp(data, verify, sizeof(verify)) == 0;
}

static bool write_headers(const fat12_remap_table_t *table,
                          fat12_remap_read_fn read,
                          fat12_remap_write_fn write, void *context) {
    uint8_t sector[FAT12_REMAP_SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, &table->header, sizeof(table->header));
    if (!write_verified(read, write, context, table->primary_sector, sector))
        return false;
    return write_verified(read, write, context, table->mirror_sector, sector);
}

bool fat12_remap_add(fat12_remap_table_t *table, uint32_t bad_sector,
        uint32_t replacement_sector, fat12_remap_read_fn read,
        fat12_remap_write_fn write, void *context) {
    if (table == NULL || read == NULL || write == NULL || bad_sector == 0U ||
        replacement_sector == 0U || bad_sector == replacement_sector ||
        table->header.entry_count >= FAT12_REMAP_MAX_ENTRIES) return false;
    for (uint32_t i = 0U; i < table->header.entry_count; ++i)
        if (table->entries[i].bad_sector == bad_sector ||
            table->entries[i].replacement_sector == replacement_sector ||
            table->entries[i].bad_sector == replacement_sector ||
            table->entries[i].replacement_sector == bad_sector)
            return false;
    uint32_t index = table->header.entry_count;
    fat12_remap_header_t previous_header = table->header;
    table->entries[index] = (fat12_remap_entry_t){
        .bad_sector = bad_sector,
        .replacement_sector = replacement_sector,
        .sequence = table->header.sequence + 1U,
    };
    uint8_t sector[FAT12_REMAP_SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    memcpy(sector, table->entries,
           (index + 1U) * sizeof(fat12_remap_entry_t));
    if (!write_verified(read, write, context, table->table_sector, sector))
        return false;
    ++table->header.entry_count;
    ++table->header.sequence;
    prepare_header(&table->header, table->media_fingerprint,
                   table->header.sequence, table->header.entry_count);
    table->header.crc32 = table_crc(&table->header, table->entries);
    if (write_headers(table, read, write, context)) return true;
    table->header = previous_header;
    memset(&table->entries[index], 0, sizeof(table->entries[index]));
    return false;
}

bool fat12_remap_lookup(const fat12_remap_table_t *table, uint32_t bad_sector,
                        uint32_t *replacement_sector) {
    if (table == NULL || replacement_sector == NULL || bad_sector == 0U)
        return false;
    for (uint32_t i = 0U; i < table->header.entry_count; ++i) {
        if (table->entries[i].bad_sector != bad_sector) continue;
        *replacement_sector = table->entries[i].replacement_sector;
        return true;
    }
    return false;
}
