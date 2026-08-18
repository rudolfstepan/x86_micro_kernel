/**
 * @file fs/fat12/fat12_journal.h
 * @brief FAT12-Undo-Journal und Recoveryvertrag.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Journalzustände und Commitwort bestimmen eindeutig Rollback oder Abschluss.
 */
#ifndef FAT12_JOURNAL_H
#define FAT12_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FAT12_JOURNAL_MAGIC 0x524A3132U /* RJ12 */
#define FAT12_JOURNAL_VERSION 2U
#define FAT12_JOURNAL_MAX_ENTRIES 64U
#define FAT12_JOURNAL_SECTOR_SIZE 512U
#define FAT12_JOURNAL_CLEAN 0U
#define FAT12_JOURNAL_ACTIVE 1U

typedef bool (*fat12_journal_read_fn)(void *context, uint32_t sector,
                                      void *buffer);
typedef bool (*fat12_journal_write_fn)(void *context, uint32_t sector,
                                       const void *buffer);

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t media_fingerprint;
    uint64_t sequence;
    uint32_t state;
    uint32_t entry_count;
    uint32_t crc32;
} fat12_journal_header_t;

typedef struct __attribute__((packed)) {
    uint32_t target_sector;
    uint32_t data_crc32;
    uint64_t sequence;
    uint32_t metadata_crc32;
} fat12_journal_entry_t;

typedef struct {
    uint32_t primary_header_sector;
    uint32_t mirror_header_sector;
    uint32_t data_start_sector;
    uint32_t media_fingerprint;
    fat12_journal_header_t header;
    fat12_journal_entry_t entries[FAT12_JOURNAL_MAX_ENTRIES];
} fat12_journal_t;

uint32_t fat12_journal_crc32(const void *data, size_t length);
bool fat12_journal_format(fat12_journal_t *journal,
                          uint32_t primary_header_sector,
                          uint32_t mirror_header_sector,
                          uint32_t data_start_sector,
                          uint32_t media_fingerprint);
bool fat12_journal_load(fat12_journal_t *journal, fat12_journal_read_fn read,
                        void *context);
bool fat12_journal_begin(fat12_journal_t *journal, uint64_t sequence,
                         fat12_journal_read_fn read,
                         fat12_journal_write_fn write, void *context);
bool fat12_journal_record(fat12_journal_t *journal, uint32_t target_sector,
                          const void *old_sector, fat12_journal_read_fn read,
                          fat12_journal_write_fn write, void *context);
bool fat12_journal_commit(fat12_journal_t *journal,
                          fat12_journal_read_fn read,
                          fat12_journal_write_fn write, void *context);
bool fat12_journal_recover(fat12_journal_t *journal, fat12_journal_read_fn read,
                           fat12_journal_write_fn write, void *context);

#endif
