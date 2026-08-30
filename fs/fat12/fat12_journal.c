/**
 * @file fs/fat12/fat12_journal.c
 * @brief Stromausfallsichere FAT12-Metadatenjournalisierung.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Write-Reihenfolge und Flush verhindern unentscheidbare Teiltransaktionen.
 */
#include "fat12_journal.h"

#include "lib/libc/string.h"

enum {
    FAT12_JOURNAL_SCRATCH_FIRST = 0,
    FAT12_JOURNAL_SCRATCH_SECOND = 1,
    FAT12_JOURNAL_SCRATCH_HEADER = 2,
    FAT12_JOURNAL_SCRATCH_VERIFY = 3,
};

static bool fat12_journal_scratch_begin(fat12_journal_t *journal) {
    if (journal == NULL) return false;
    uint8_t expected = 0U;
    return __atomic_compare_exchange_n(&journal->scratch.in_use, &expected,
        1U, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void fat12_journal_scratch_end(fat12_journal_t *journal) {
    if (journal == NULL) return;
    memset(journal->scratch.sectors, 0, sizeof(journal->scratch.sectors));
    __atomic_store_n(&journal->scratch.in_use, 0U, __ATOMIC_RELEASE);
}

static bool header_valid(const fat12_journal_header_t *header,
                         uint32_t fingerprint) {
    if (header == NULL || header->magic != FAT12_JOURNAL_MAGIC ||
        header->version != FAT12_JOURNAL_VERSION ||
        header->header_size != sizeof(*header) ||
        header->media_fingerprint != fingerprint ||
        header->state > FAT12_JOURNAL_ACTIVE ||
        header->entry_count > FAT12_JOURNAL_MAX_ENTRIES) return false;
    fat12_journal_header_t copy = *header;
    uint32_t expected = copy.crc32;
    copy.crc32 = 0U;
    return expected == fat12_journal_crc32(&copy, sizeof(copy));
}

static void prepare_header(fat12_journal_header_t *header,
                           uint32_t fingerprint, uint64_t sequence,
                           uint32_t state, uint32_t entry_count) {
    *header = (fat12_journal_header_t){
        .magic = FAT12_JOURNAL_MAGIC,
        .version = FAT12_JOURNAL_VERSION,
        .header_size = sizeof(*header),
        .media_fingerprint = fingerprint,
        .sequence = sequence,
        .state = state,
        .entry_count = entry_count,
        .crc32 = 0U,
    };
    header->crc32 = fat12_journal_crc32(header, sizeof(*header));
}

static bool write_verified(fat12_journal_t *journal,
        fat12_journal_read_fn read,
        fat12_journal_write_fn write, void *context, uint32_t sector,
        const void *data) {
    uint8_t *verify = journal->scratch.sectors[
        FAT12_JOURNAL_SCRATCH_VERIFY];
    return journal != NULL && read != NULL && write != NULL && data != NULL &&
           write(context, sector, data) && read(context, sector, verify) &&
           memcmp(data, verify, FAT12_JOURNAL_SECTOR_SIZE) == 0;
}

static bool write_header(fat12_journal_t *journal,
                         fat12_journal_read_fn read,
                         fat12_journal_write_fn write, void *context) {
    uint8_t *sector = journal->scratch.sectors[
        FAT12_JOURNAL_SCRATCH_HEADER];
    memset(sector, 0, FAT12_JOURNAL_SECTOR_SIZE);
    memcpy(sector, &journal->header, sizeof(journal->header));
    if (!write_verified(journal, read, write, context,
                        journal->primary_header_sector, sector)) return false;
    return write_verified(journal, read, write, context,
                          journal->mirror_header_sector, sector);
}

uint32_t fat12_journal_crc32(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    if (bytes == NULL && length != 0U) return 0U;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (uint32_t)-(int32_t)(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFU;
}

bool fat12_journal_format(fat12_journal_t *journal,
        uint32_t primary_header_sector, uint32_t mirror_header_sector,
        uint32_t data_start_sector, uint32_t media_fingerprint) {
    uint32_t last_header = primary_header_sector > mirror_header_sector
        ? primary_header_sector : mirror_header_sector;
    if (journal == NULL || media_fingerprint == 0U ||
        primary_header_sector == mirror_header_sector ||
        data_start_sector <= last_header ||
        data_start_sector > UINT32_MAX - (FAT12_JOURNAL_MAX_ENTRIES * 2U - 1U))
        return false;
    memset(journal, 0, sizeof(*journal));
    journal->primary_header_sector = primary_header_sector;
    journal->mirror_header_sector = mirror_header_sector;
    journal->data_start_sector = data_start_sector;
    journal->media_fingerprint = media_fingerprint;
    prepare_header(&journal->header, media_fingerprint, 1U,
                   FAT12_JOURNAL_CLEAN, 0U);
    return true;
}

static bool fat12_journal_load_locked(fat12_journal_t *journal,
        fat12_journal_read_fn read, void *context) {
    if (journal == NULL || read == NULL || journal->media_fingerprint == 0U)
        return false;
    uint8_t *primary = journal->scratch.sectors[
        FAT12_JOURNAL_SCRATCH_FIRST];
    uint8_t *mirror = journal->scratch.sectors[
        FAT12_JOURNAL_SCRATCH_SECOND];
    fat12_journal_header_t first, second;
    bool first_ok = read(context, journal->primary_header_sector, primary) &&
                    (memcpy(&first, primary, sizeof(first)),
                     header_valid(&first, journal->media_fingerprint));
    bool second_ok = read(context, journal->mirror_header_sector, mirror) &&
                     (memcpy(&second, mirror, sizeof(second)),
                     header_valid(&second, journal->media_fingerprint));
    if (!first_ok && !second_ok) return false;
    if (first_ok && second_ok && first.sequence == second.sequence &&
        memcmp(&first, &second, sizeof(first)) != 0) return false;
    journal->header = !second_ok || (first_ok && first.sequence >= second.sequence)
        ? first : second;
    return true;
}

bool fat12_journal_load(fat12_journal_t *journal, fat12_journal_read_fn read,
                        void *context) {
    if (!fat12_journal_scratch_begin(journal)) return false;
    bool result = fat12_journal_load_locked(journal, read, context);
    fat12_journal_scratch_end(journal);
    return result;
}

static bool fat12_journal_begin_locked(fat12_journal_t *journal,
        uint64_t sequence, fat12_journal_read_fn read,
        fat12_journal_write_fn write, void *context) {
    if (journal == NULL || read == NULL || write == NULL ||
        journal->media_fingerprint == 0U ||
        journal->header.state == FAT12_JOURNAL_ACTIVE ||
        sequence <= journal->header.sequence)
        return false;
    memset(journal->entries, 0, sizeof(journal->entries));
    prepare_header(&journal->header, journal->media_fingerprint, sequence,
                   FAT12_JOURNAL_ACTIVE, 0U);
    return write_header(journal, read, write, context);
}

bool fat12_journal_begin(fat12_journal_t *journal, uint64_t sequence,
                         fat12_journal_read_fn read,
                         fat12_journal_write_fn write, void *context) {
    if (!fat12_journal_scratch_begin(journal)) return false;
    bool result = fat12_journal_begin_locked(journal, sequence, read, write,
                                              context);
    fat12_journal_scratch_end(journal);
    return result;
}

static bool fat12_journal_record_locked(fat12_journal_t *journal,
        uint32_t target_sector,
        const void *old_sector, fat12_journal_read_fn read,
        fat12_journal_write_fn write, void *context) {
    uint32_t journal_end = journal != NULL
        ? journal->data_start_sector + FAT12_JOURNAL_MAX_ENTRIES * 2U : 0U;
    if (journal == NULL || old_sector == NULL || read == NULL || write == NULL ||
        journal->header.state != FAT12_JOURNAL_ACTIVE ||
        target_sector == journal->primary_header_sector ||
        target_sector == journal->mirror_header_sector ||
        (target_sector >= journal->data_start_sector &&
         target_sector < journal_end)) return false;
    for (uint32_t index = 0U; index < journal->header.entry_count; ++index)
        if (journal->entries[index].target_sector == target_sector) return true;
    if (journal->header.entry_count >= FAT12_JOURNAL_MAX_ENTRIES) return false;
    uint32_t index = journal->header.entry_count;
    fat12_journal_entry_t *entry = &journal->entries[index];
    entry->target_sector = target_sector;
    entry->sequence = journal->header.sequence;
    entry->data_crc32 = fat12_journal_crc32(old_sector,
                                             FAT12_JOURNAL_SECTOR_SIZE);
    entry->metadata_crc32 = 0U;
    entry->metadata_crc32 = fat12_journal_crc32(entry, sizeof(*entry));
    uint8_t *metadata = journal->scratch.sectors[
        FAT12_JOURNAL_SCRATCH_FIRST];
    memset(metadata, 0, FAT12_JOURNAL_SECTOR_SIZE);
    memcpy(metadata, entry, sizeof(*entry));
    if (!write_verified(journal, read, write, context,
            journal->data_start_sector + index * 2U, old_sector) ||
        !write_verified(journal, read, write, context,
            journal->data_start_sector + index * 2U + 1U, metadata))
        return false;
    ++journal->header.entry_count;
    prepare_header(&journal->header, journal->media_fingerprint,
                   journal->header.sequence, FAT12_JOURNAL_ACTIVE,
                   journal->header.entry_count);
    return write_header(journal, read, write, context);
}

bool fat12_journal_record(fat12_journal_t *journal, uint32_t target_sector,
        const void *old_sector, fat12_journal_read_fn read,
        fat12_journal_write_fn write, void *context) {
    if (!fat12_journal_scratch_begin(journal)) return false;
    bool result = fat12_journal_record_locked(journal, target_sector,
        old_sector, read, write, context);
    fat12_journal_scratch_end(journal);
    return result;
}

static bool fat12_journal_commit_locked(fat12_journal_t *journal,
        fat12_journal_read_fn read, fat12_journal_write_fn write,
        void *context) {
    if (journal == NULL || read == NULL || write == NULL ||
        journal->header.state != FAT12_JOURNAL_ACTIVE) return false;
    uint32_t entry_count = journal->header.entry_count;
    uint64_t sequence = journal->header.sequence;
    prepare_header(&journal->header, journal->media_fingerprint,
                   sequence, FAT12_JOURNAL_CLEAN, 0U);
    if (write_header(journal, read, write, context)) return true;
    /* Keep the in-memory volume fenced after an uncertain CLEAN write. */
    prepare_header(&journal->header, journal->media_fingerprint,
                   sequence, FAT12_JOURNAL_ACTIVE, entry_count);
    return false;
}

bool fat12_journal_commit(fat12_journal_t *journal,
                          fat12_journal_read_fn read,
                          fat12_journal_write_fn write, void *context) {
    if (!fat12_journal_scratch_begin(journal)) return false;
    bool result = fat12_journal_commit_locked(journal, read, write, context);
    fat12_journal_scratch_end(journal);
    return result;
}

static bool fat12_journal_recover_locked(fat12_journal_t *journal,
        fat12_journal_read_fn read, fat12_journal_write_fn write,
        void *context) {
    if (journal == NULL || read == NULL || write == NULL ||
        !fat12_journal_load_locked(journal, read, context)) return false;
    if (journal->header.state == FAT12_JOURNAL_CLEAN) return true;
    for (uint32_t index = 0U; index < journal->header.entry_count; ++index) {
        fat12_journal_entry_t entry;
        uint8_t *old_sector = journal->scratch.sectors[
            FAT12_JOURNAL_SCRATCH_FIRST];
        uint8_t *metadata = journal->scratch.sectors[
            FAT12_JOURNAL_SCRATCH_SECOND];
        uint8_t *verify = journal->scratch.sectors[
            FAT12_JOURNAL_SCRATCH_VERIFY];
        if (!read(context, journal->data_start_sector + index * 2U,
                  old_sector) ||
            !read(context, journal->data_start_sector + index * 2U + 1U,
                 metadata)) return false;
        memcpy(&entry, metadata, sizeof(entry));
        uint32_t metadata_crc = entry.metadata_crc32;
        entry.metadata_crc32 = 0U;
        if (entry.target_sector == journal->primary_header_sector ||
            entry.target_sector == journal->mirror_header_sector ||
            entry.sequence != journal->header.sequence ||
            metadata_crc != fat12_journal_crc32(&entry, sizeof(entry)) ||
            entry.data_crc32 != fat12_journal_crc32(old_sector,
                FAT12_JOURNAL_SECTOR_SIZE) ||
            !write(context, entry.target_sector, old_sector) ||
            !read(context, entry.target_sector, verify) ||
            memcmp(old_sector, verify, FAT12_JOURNAL_SECTOR_SIZE) != 0)
            return false;
    }
    return fat12_journal_commit_locked(journal, read, write, context);
}

bool fat12_journal_recover(fat12_journal_t *journal, fat12_journal_read_fn read,
        fat12_journal_write_fn write, void *context) {
    if (!fat12_journal_scratch_begin(journal)) return false;
    bool result = fat12_journal_recover_locked(journal, read, write, context);
    fat12_journal_scratch_end(journal);
    return result;
}
