/**
 * @file drivers/block/ata_journal.c
 * @brief Fixed-capacity redundant ATA undo-journal state machine.
 */
#include "drivers/block/ata_journal.h"
#include "lib/libc/string.h"

#include <limits.h>

typedef struct __attribute__((packed)) {
    uint32_t magic, version, state, target_lba, data_crc32, sequence;
    uint32_t header_crc32;
    uint8_t reserved[ATA_JOURNAL_SECTOR_SIZE - 28U];
} ata_journal_v1_record_t;

_Static_assert(sizeof(ata_journal_record_t) == ATA_JOURNAL_SECTOR_SIZE,
               "ATA journal v2 record must remain one sector");
_Static_assert(sizeof(ata_journal_v1_record_t) == ATA_JOURNAL_SECTOR_SIZE,
               "ATA journal v1 record must remain one sector");

static uint32_t journal_crc32_update(uint32_t crc, const void *data,
                                     size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

static uint32_t journal_crc32(const void *data, size_t length) {
    uint32_t crc = journal_crc32_update(0xFFFFFFFFU, data, length);
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t journal_record_crc32(const ata_journal_record_t *record) {
    static const uint32_t zero = 0U;
    const size_t crc_offset = offsetof(ata_journal_record_t, header_crc32);
    uint32_t crc = journal_crc32_update(0xFFFFFFFFU, record, crc_offset);
    crc = journal_crc32_update(crc, &zero, sizeof(zero));
    crc = journal_crc32_update(
        crc, (const uint8_t *)record + crc_offset + sizeof(uint32_t),
        sizeof(*record) - crc_offset - sizeof(uint32_t));
    return crc ^ 0xFFFFFFFFU;
}

static bool record_valid(const ata_journal_record_t *record) {
    uint32_t expected_crc = record->header_crc32;
    return record->magic == ATA_JOURNAL_MAGIC &&
           record->version == ATA_JOURNAL_VERSION &&
           record->state <= ATA_JOURNAL_ACTIVE &&
           record->entry_count <= ATA_JOURNAL_MAX_ENTRIES &&
           (record->state == ATA_JOURNAL_ACTIVE ||
            record->entry_count == 0U) &&
           expected_crc == journal_record_crc32(record);
}

static void seal_record(ata_journal_record_t *record) {
    record->header_crc32 = 0U;
    record->header_crc32 = journal_record_crc32(record);
}

static bool v1_record_valid(const ata_journal_v1_record_t *record) {
    return record->magic == ATA_JOURNAL_MAGIC && record->version == 1U &&
           record->state <= ATA_JOURNAL_ACTIVE &&
           record->header_crc32 == journal_crc32(record, 24U);
}

static bool transport_ready(const ata_undo_journal_t *journal) {
    return journal != NULL && journal->transport != NULL &&
           journal->transport->read != NULL &&
           journal->transport->write != NULL;
}

static bool deferred_transport_ready(const ata_undo_journal_t *journal) {
    return transport_ready(journal) &&
           journal->transport->write_deferred != NULL &&
           journal->transport->flush != NULL;
}

static bool deferred_commit_ready(const ata_undo_journal_t *journal) {
    return deferred_transport_ready(journal) &&
           journal->transport->commit_begin != NULL &&
           journal->transport->commit_write_deferred != NULL &&
           journal->transport->commit_end != NULL;
}

static bool read_sector(ata_undo_journal_t *journal, unsigned short base,
                        uint32_t lba, void *buffer, bool is_master) {
    return transport_ready(journal) &&
        journal->transport->read(journal->transport_context, base, lba,
                                 buffer, is_master);
}

static bool write_sector(ata_undo_journal_t *journal, unsigned short base,
                         uint32_t lba, const void *buffer, bool is_master) {
    return transport_ready(journal) &&
        journal->transport->write(journal->transport_context, base, lba,
                                  buffer, is_master);
}

static bool write_sector_deferred(ata_undo_journal_t *journal,
                                  unsigned short base, uint32_t lba,
                                  const void *buffer, bool is_master) {
    return deferred_transport_ready(journal) &&
        journal->transport->write_deferred(journal->transport_context, base,
                                           lba, buffer, is_master);
}

static bool flush_deferred(ata_undo_journal_t *journal) {
    return deferred_transport_ready(journal) &&
        journal->transport->flush(journal->transport_context, journal->base,
                                  journal->is_master);
}

static bool write_record(ata_undo_journal_t *journal,
                         const ata_journal_record_t *record,
                         bool deferred) {
    ata_journal_write_fn writer = deferred
        ? journal->transport->write_deferred : journal->transport->write;
    if (writer == NULL ||
        !writer(journal->transport_context, journal->base,
                journal->header_lba, record, journal->is_master)) return false;
    return journal->mirror_lba == 0U ||
           writer(journal->transport_context, journal->base,
                  journal->mirror_lba, record, journal->is_master);
}

void ata_undo_journal_make_clean(ata_journal_record_t *record,
                                 uint32_t sequence) {
    if (record == NULL) return;
    memset(record, 0, sizeof(*record));
    record->magic = ATA_JOURNAL_MAGIC;
    record->version = ATA_JOURNAL_VERSION;
    record->state = ATA_JOURNAL_CLEAN;
    record->sequence = sequence;
    seal_record(record);
}

static bool clear_journal(ata_undo_journal_t *journal, bool deferred) {
    ata_journal_record_t *clean = &journal->scratch.primary;
    ata_undo_journal_make_clean(clean, journal->sequence);
    return write_record(journal, clean, deferred);
}

static bool write_active(ata_undo_journal_t *journal, bool deferred) {
    ata_journal_record_t *active = &journal->scratch.primary;
    memset(active, 0, sizeof(*active));
    active->magic = ATA_JOURNAL_MAGIC;
    active->version = ATA_JOURNAL_VERSION;
    active->state = ATA_JOURNAL_ACTIVE;
    active->sequence = journal->sequence;
    active->entry_count = journal->entry_count;
    for (uint32_t i = 0U; i < journal->entry_count; ++i) {
        active->entries[i].target_lba = journal->entries[i].target_lba;
        active->entries[i].data_crc32 = journal->entries[i].data_crc32;
    }
    seal_record(active);
    return write_record(journal, active, deferred);
}

void ata_undo_journal_init(ata_undo_journal_t *journal,
                           const ata_journal_transport_t *transport,
                           void *transport_context) {
    if (journal == NULL) return;
    memset(journal, 0, sizeof(*journal));
    journal->transport = transport;
    journal->transport_context = transport_context;
}

bool ata_undo_journal_transaction_begin(ata_undo_journal_t *journal) {
    if (journal == NULL || journal->transaction_depth == UINT32_MAX)
        return false;
    if (journal->transaction_depth++ == 0U) journal->entry_count = 0U;
    return true;
}

static bool commit_targets_deferred(ata_undo_journal_t *journal) {
    if (!deferred_commit_ready(journal)) return false;
    if (journal->transport->commit_write_sectors_deferred == NULL) {
        for (uint32_t index = 0U; index < journal->entry_count; ++index) {
            if (!journal->transport->commit_write_deferred(
                    journal->transport_context, journal->base,
                    journal->entries[index].target_lba,
                    journal->pending_data[index], journal->is_master))
                return false;
        }
        return true;
    }

    /* Entry order is the validated mutation order. Only adjacent LBAs are
     * combined, so no unjournaled gap can enter one ATA command. */
    for (uint32_t index = 0U; index < journal->entry_count;) {
        uint32_t count = 1U;
        while (index + count < journal->entry_count &&
               journal->entries[index + count - 1U].target_lba !=
                   UINT32_MAX &&
               journal->entries[index + count].target_lba ==
                   journal->entries[index + count - 1U].target_lba + 1U) {
            ++count;
        }
        if (!journal->transport->commit_write_sectors_deferred(
                journal->transport_context, journal->base,
                journal->entries[index].target_lba, count,
                journal->pending_data[index], journal->is_master))
            return false;
        index += count;
    }
    return true;
}

static bool transaction_end(ata_undo_journal_t *journal, bool commit,
                            bool use_commit_transport) {
    if (journal == NULL || journal->transaction_depth == 0U) return false;
    if (--journal->transaction_depth != 0U) return true;
    if (!journal->enabled) {
        journal->entry_count = 0U;
        return true;
    }
    bool result = true;
    if (journal->entry_count != 0U && commit) {
        bool deferred = deferred_transport_ready(journal);
        /* Targets remain untouched until every undo sector is durable. The
         * ACTIVE record is then published exactly once for the complete
         * transaction. This preserves recovery ordering while reducing a
         * transaction to four fixed barriers: undo, ACTIVE, targets, CLEAN. */
        if (deferred &&
            journal->transport->write_sectors_deferred != NULL) {
            result = journal->transport->write_sectors_deferred(
                journal->transport_context, journal->base, journal->data_lba,
                journal->entry_count, journal->undo_data,
                journal->is_master);
        } else {
            for (uint32_t i = 0U; result && i < journal->entry_count; ++i) {
                result = deferred
                    ? write_sector_deferred(journal, journal->base,
                        journal->data_lba + i, journal->undo_data[i],
                        journal->is_master)
                    : write_sector(journal, journal->base,
                        journal->data_lba + i, journal->undo_data[i],
                        journal->is_master);
            }
        }
        if (result && deferred) result = flush_deferred(journal);
        if (result) result = write_active(journal, deferred);
        if (result && deferred) result = flush_deferred(journal);
        if (result && use_commit_transport &&
            deferred_commit_ready(journal)) {
            bool begun = journal->transport->commit_begin(
                journal->transport_context, journal->base,
                journal->is_master);
            result = begun;
            if (result) result = commit_targets_deferred(journal);
            if (begun) {
                bool targets_written = result;
                bool ended = journal->transport->commit_end(
                    journal->transport_context, journal->base,
                    journal->is_master, targets_written);
                result = targets_written && ended;
            }
        } else if (result && !use_commit_transport && deferred) {
            for (uint32_t i = 0U; result && i < journal->entry_count; ++i) {
                result = write_sector_deferred(journal, journal->base,
                    journal->entries[i].target_lba, journal->pending_data[i],
                    journal->is_master);
            }
            if (result) result = flush_deferred(journal);
        } else {
            for (uint32_t i = 0U; result && i < journal->entry_count; ++i) {
                if (use_commit_transport &&
                    journal->transport->commit_write != NULL) {
                    result = journal->transport->commit_write(
                        journal->transport_context, journal->base,
                        journal->entries[i].target_lba,
                        journal->pending_data[i], journal->is_master);
                } else {
                    result = write_sector(journal, journal->base,
                        journal->entries[i].target_lba,
                        journal->pending_data[i], journal->is_master);
                }
            }
        }
        if (result) result = clear_journal(journal, deferred);
        if (result && deferred) result = flush_deferred(journal);
    }
    journal->entry_count = 0U;
    return result;
}

bool ata_undo_journal_transaction_end(ata_undo_journal_t *journal,
                                      bool commit) {
    return transaction_end(journal, commit, true);
}

bool ata_undo_journal_read_sector(ata_undo_journal_t *journal,
                                  unsigned short base, uint32_t lba,
                                  void *buffer, bool is_master) {
    if (!transport_ready(journal) || buffer == NULL) return false;
    if (journal->enabled && journal->transaction_depth != 0U &&
        base == journal->base && is_master == journal->is_master) {
        for (uint32_t i = 0U; i < journal->entry_count; ++i) {
            if (journal->entries[i].target_lba == lba) {
                memcpy(buffer, journal->pending_data[i],
                       ATA_JOURNAL_SECTOR_SIZE);
                return true;
            }
        }
    }
    return read_sector(journal, base, lba, buffer, is_master);
}

bool ata_undo_journal_write_sector(ata_undo_journal_t *journal,
                                   unsigned short base, uint32_t lba,
                                   const void *buffer, bool is_master) {
    if (!transport_ready(journal) || buffer == NULL) return false;
    if (!journal->enabled || base != journal->base ||
        is_master != journal->is_master ||
        lba < journal->volume_start_lba || lba >= journal->volume_end_lba)
        return write_sector(journal, base, lba, buffer, is_master);
    if ((lba >= journal->header_lba &&
         lba < journal->data_lba + ATA_JOURNAL_MAX_ENTRIES) ||
        lba == journal->mirror_lba) return false;

    bool automatic = journal->transaction_depth == 0U;
    if (automatic && !ata_undo_journal_transaction_begin(journal))
        return false;
    for (uint32_t i = 0U; i < journal->entry_count; ++i) {
        if (journal->entries[i].target_lba == lba) {
            memcpy(journal->pending_data[i], buffer,
                   ATA_JOURNAL_SECTOR_SIZE);
            bool result = true;
            return automatic
                ? (transaction_end(journal, result, false) && result)
                : result;
        }
    }
    if (journal->entry_count >= ATA_JOURNAL_MAX_ENTRIES) {
        if (automatic)
            (void)transaction_end(journal, false, false);
        return false;
    }

    uint32_t slot = journal->entry_count;
    bool result = read_sector(journal, base, lba, journal->undo_data[slot],
                              is_master);
    if (result) {
        journal->entries[slot].target_lba = lba;
        journal->entries[slot].data_crc32 =
            journal_crc32(journal->undo_data[slot],
                          ATA_JOURNAL_SECTOR_SIZE);
        memcpy(journal->pending_data[slot], buffer,
               ATA_JOURNAL_SECTOR_SIZE);
        journal->entry_count++;
        if (slot == 0U && ++journal->sequence == 0U) result = false;
    }
    if (automatic)
        result = transaction_end(journal, result, false) && result;
    return result;
}

bool ata_undo_journal_attach(ata_undo_journal_t *journal,
                             unsigned short base, bool is_master,
                             uint32_t partition_lba, uint32_t volume_sectors,
                             uint16_t reserved_sectors) {
    if (!transport_ready(journal)) return false;
    bool result = true;
    uint32_t inherited_depth = journal->transaction_depth;
    journal->enabled = false;
    if (inherited_depth != 0U && journal->entry_count != 0U) return false;
    if (reserved_sectors <= ATA_JOURNAL_DATA_OFFSET || volume_sectors == 0U ||
        partition_lba > UINT32_MAX - ATA_JOURNAL_DATA_OFFSET ||
        volume_sectors > UINT32_MAX - partition_lba) return true;

    uint32_t header_lba = partition_lba + ATA_JOURNAL_HEADER_OFFSET;
    uint32_t data_lba = partition_lba + ATA_JOURNAL_DATA_OFFSET;
    uint32_t mirror_lba = reserved_sectors > ATA_JOURNAL_MIRROR_OFFSET
        ? partition_lba + ATA_JOURNAL_MIRROR_OFFSET : 0U;
    ata_journal_record_t *primary = &journal->scratch.primary;
    ata_journal_record_t *mirror = &journal->scratch.mirror;
    bool primary_read = read_sector(journal, base, header_lba, primary,
                                    is_master);
    bool mirror_read = mirror_lba != 0U &&
        read_sector(journal, base, mirror_lba, mirror, is_master);
    bool primary_marked = primary_read &&
        primary->magic == ATA_JOURNAL_MAGIC;
    bool mirror_marked = mirror_read && mirror->magic == ATA_JOURNAL_MAGIC;
    if (!primary_marked && !mirror_marked) return true;

    journal->base = base;
    journal->is_master = is_master;
    journal->header_lba = header_lba;
    journal->mirror_lba = mirror_lba;
    journal->data_lba = data_lba;
    journal->volume_start_lba = partition_lba;
    journal->volume_end_lba = partition_lba + volume_sectors;
    journal->entry_count = 0U;
    journal->transaction_depth = inherited_depth;

    bool primary_valid = primary_marked &&
        (primary->version == 1U
            ? v1_record_valid((const ata_journal_v1_record_t *)primary)
            : record_valid(primary));
    bool mirror_valid = mirror_marked &&
        mirror->version == ATA_JOURNAL_VERSION && record_valid(mirror);
    bool repair_headers = !primary_valid || (mirror_lba != 0U && !mirror_valid);
    if (!primary_valid && !mirror_valid) return false;
    const ata_journal_record_t *record;
    if (primary_valid && mirror_valid &&
        primary->version == ATA_JOURNAL_VERSION) {
        if (primary->sequence > mirror->sequence) {
            record = primary;
            repair_headers = true;
        } else if (mirror->sequence > primary->sequence) {
            record = mirror;
            repair_headers = true;
        } else if (primary->state != mirror->state) {
            record = primary->state == ATA_JOURNAL_ACTIVE ? primary : mirror;
            repair_headers = true;
        } else if (memcmp(primary, mirror, sizeof(*primary)) != 0) {
            return false;
        } else {
            record = primary;
        }
    } else if (mirror_valid &&
               (!primary_valid || primary->version != ATA_JOURNAL_VERSION)) {
        record = mirror;
        repair_headers = true;
    } else {
        record = primary;
        repair_headers = true;
    }

    if (record->version == 1U) {
        const ata_journal_v1_record_t *old =
            (const ata_journal_v1_record_t *)record;
        journal->sequence = old->sequence;
        if (old->state == ATA_JOURNAL_ACTIVE) {
            uint8_t *data = journal->scratch.data;
            result = old->target_lba >= journal->volume_start_lba &&
                old->target_lba < journal->volume_end_lba &&
                read_sector(journal, base, data_lba, data, is_master) &&
                journal_crc32(data, ATA_JOURNAL_SECTOR_SIZE) ==
                    old->data_crc32 &&
                write_sector(journal, base, old->target_lba, data, is_master);
        }
        if (result) result = clear_journal(journal, false);
    } else {
        result = reserved_sectors >
                     ATA_JOURNAL_DATA_OFFSET + ATA_JOURNAL_MAX_ENTRIES - 1U &&
                 record_valid(record);
        journal->sequence = record->sequence;
        for (uint32_t i = record->entry_count; result && i > 0U; --i) {
            uint32_t index = i - 1U;
            uint32_t target = record->entries[index].target_lba;
            uint8_t *data = journal->scratch.data;
            result = target >= journal->volume_start_lba &&
                target < journal->volume_end_lba &&
                !(target >= header_lba &&
                  target < data_lba + ATA_JOURNAL_MAX_ENTRIES) &&
                read_sector(journal, base, data_lba + index, data,
                            is_master) &&
                journal_crc32(data, ATA_JOURNAL_SECTOR_SIZE) ==
                    record->entries[index].data_crc32 &&
                write_sector(journal, base, target, data, is_master);
        }
        if (result &&
            (record->state == ATA_JOURNAL_ACTIVE || repair_headers))
            result = clear_journal(journal, false);
    }
    if (result) journal->enabled = true;
    return result;
}

bool ata_undo_journal_is_attached(const ata_undo_journal_t *journal,
                                  unsigned short base, bool is_master,
                                  uint32_t partition_lba,
                                  uint32_t volume_sectors) {
    return journal != NULL && journal->enabled && volume_sectors != 0U &&
        partition_lba <= UINT32_MAX - volume_sectors &&
        journal->base == base && journal->is_master == is_master &&
        journal->volume_start_lba == partition_lba &&
        journal->volume_end_lba == partition_lba + volume_sectors;
}
