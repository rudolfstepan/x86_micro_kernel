/* Ring-3 host adapter for the existing RSTJ v1/v2 undo core. No ambient OS
 * calls: the host explicitly supplies token/guard mediation. Fixed storage;
 * an instance must not live on a small task stack or be shared concurrently. */
#ifndef REIST_FAT32_TRANSACTION_H
#define REIST_FAT32_TRANSACTION_H
#include "reist/abi/syscall.h"
#include "../../../../drivers/block/ata_journal.h"

typedef struct {
    void* context;
    int (*guard)(void*, reist_file_object_guard_request_t*);
    int (*transfer)(void*, const reist_storage_journal_request_t*, void*);
} reist_fat32_transaction_io_t;

typedef struct {
    ata_undo_journal_t journal;
    reist_fat32_transaction_io_t io;
    uint32_t resource, token, first, sectors;
    int error;
    bool active, attempted;
} reist_fat32_transaction_t;

/* Zero-initialize once. An admitted context cannot be reopened/reinitialized
 * before finish; malformed geometry is rejected before asking for authority. */
int reist_fat32_transaction_begin(reist_fat32_transaction_t* transaction,
    const reist_fat32_transaction_io_t* io, const reist_file_object_key_t* key,
    uint32_t first, uint32_t sectors, uint16_t reserved, uint64_t deadline_ms);
int reist_fat32_transaction_stage(reist_fat32_transaction_t* transaction,
    uint32_t sector, const void* data);
int reist_fat32_transaction_read(reist_fat32_transaction_t* transaction,
    uint32_t sector, void* data);
/* Finishes/revokes the short reservation even after a failed commit. Never
 * retries an uncertain transaction or silently reacquires authority. */
int reist_fat32_transaction_finish(reist_fat32_transaction_t* transaction,
                                   bool commit, uint32_t* outcome);
#endif
