/**
 * @file drivers/block/ata_journal.h
 * @brief Transport-neutral fixed-capacity ATA undo-journal core.
 *
 * Layer: Ring-0 block persistence mechanism.
 * Contract: Version-2 on-disk records and write ordering remain independent of
 * the ATA or AHCI transport which persists one complete 512-byte sector.
 * Safety: The core allocates no memory and accepts at most twenty unique
 * target sectors in one transaction.
 */
#ifndef ATA_JOURNAL_H
#define ATA_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_JOURNAL_SECTOR_SIZE 512U
#define ATA_JOURNAL_MAGIC 0x4A545352U /* "RSTJ" */
#define ATA_JOURNAL_VERSION 2U
#define ATA_JOURNAL_CLEAN 0U
#define ATA_JOURNAL_ACTIVE 1U
#define ATA_JOURNAL_HEADER_OFFSET 8U
#define ATA_JOURNAL_DATA_OFFSET 9U
#define ATA_JOURNAL_MAX_ENTRIES 20U
#define ATA_JOURNAL_MIRROR_OFFSET 31U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t state;
    uint32_t sequence;
    uint32_t entry_count;
    uint32_t header_crc32;
    struct {
        uint32_t target_lba;
        uint32_t data_crc32;
    } entries[ATA_JOURNAL_MAX_ENTRIES];
    uint8_t reserved[ATA_JOURNAL_SECTOR_SIZE - 24U -
                     ATA_JOURNAL_MAX_ENTRIES * 8U];
} ata_journal_record_t;

typedef bool (*ata_journal_read_fn)(void *context, unsigned short base,
                                    uint32_t lba, void *buffer,
                                    bool is_master);
typedef bool (*ata_journal_write_fn)(void *context, unsigned short base,
                                     uint32_t lba, const void *buffer,
                                     bool is_master);
typedef bool (*ata_journal_write_sectors_fn)(void *context,
                                             unsigned short base,
                                             uint32_t lba, uint32_t count,
                                             const void *buffer,
                                             bool is_master);
typedef bool (*ata_journal_flush_fn)(void *context, unsigned short base,
                                     bool is_master);
typedef bool (*ata_journal_commit_begin_fn)(void *context,
                                            unsigned short base,
                                            bool is_master);
typedef bool (*ata_journal_commit_end_fn)(void *context,
                                          unsigned short base,
                                          bool is_master, bool commit);

typedef struct {
    ata_journal_read_fn read;
    ata_journal_write_fn write;
    ata_journal_write_fn commit_write;
    /* Optional pair used to coalesce cache flushes without weakening the
     * on-disk ordering. Both callbacks must be present or neither is used. */
    ata_journal_write_fn write_deferred;
    ata_journal_write_sectors_fn write_sectors_deferred;
    ata_journal_flush_fn flush;
    /* Optional target batch. Begin owns the transport and supervision until
     * end publishes the final durability result. */
    ata_journal_commit_begin_fn commit_begin;
    ata_journal_write_fn commit_write_deferred;
    ata_journal_commit_end_fn commit_end;
} ata_journal_transport_t;

typedef struct {
    bool enabled;
    unsigned short base;
    bool is_master;
    uint32_t header_lba;
    uint32_t mirror_lba;
    uint32_t data_lba;
    uint32_t volume_start_lba;
    uint32_t volume_end_lba;
    uint32_t sequence;
    uint32_t entry_count;
    uint32_t transaction_depth;
    struct {
        uint32_t target_lba;
        uint32_t data_crc32;
    } entries[ATA_JOURNAL_MAX_ENTRIES];
    uint8_t undo_data[ATA_JOURNAL_MAX_ENTRIES][ATA_JOURNAL_SECTOR_SIZE];
    uint8_t pending_data[ATA_JOURNAL_MAX_ENTRIES][ATA_JOURNAL_SECTOR_SIZE];
    const ata_journal_transport_t *transport;
    void *transport_context;
} ata_undo_journal_t;

void ata_undo_journal_init(ata_undo_journal_t *journal,
                           const ata_journal_transport_t *transport,
                           void *transport_context);
void ata_undo_journal_make_clean(ata_journal_record_t *record,
                                 uint32_t sequence);
bool ata_undo_journal_attach(ata_undo_journal_t *journal,
                             unsigned short base, bool is_master,
                             uint32_t partition_lba, uint32_t volume_sectors,
                             uint16_t reserved_sectors);
bool ata_undo_journal_is_attached(const ata_undo_journal_t *journal,
                                  unsigned short base, bool is_master,
                                  uint32_t partition_lba,
                                  uint32_t volume_sectors);
bool ata_undo_journal_transaction_begin(ata_undo_journal_t *journal);
bool ata_undo_journal_transaction_end(ata_undo_journal_t *journal,
                                      bool commit);
bool ata_undo_journal_read_sector(ata_undo_journal_t *journal,
                                  unsigned short base, uint32_t lba,
                                  void *buffer, bool is_master);
bool ata_undo_journal_write_sector(ata_undo_journal_t *journal,
                                   unsigned short base, uint32_t lba,
                                   const void *buffer, bool is_master);

#endif
