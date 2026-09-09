#include "../include/reist/fat32_transaction.h"
#include "../../../lib/libc/string.h"

static int tx_error(reist_fat32_transaction_t* tx, int error) {
    if (!tx->error) tx->error = error;
    return tx->error;
}

static bool tx_io(reist_fat32_transaction_t* tx, uint32_t operation,
                  uint32_t sector, uint32_t count, void* data) {
    if (!tx->active || !tx->token || tx->error) return false;
    bool flush = operation == REIST_STORAGE_JOURNAL_FLUSH;
    if (!flush && (sector < tx->first || sector - tx->first >= tx->sectors ||
        !count || count > REIST_STORAGE_JOURNAL_MAX_SECTORS ||
        count > tx->sectors - (sector - tx->first))) {
        tx_error(tx, -REIST_EINVAL);
        return false;
    }
    reist_storage_journal_request_t request = {
        REIST_STORAGE_JOURNAL_VERSION, sizeof(request), operation, tx->token,
        tx->resource, sector, count, 0
    };
    if (operation == REIST_STORAGE_JOURNAL_WRITE_DEFERRED) tx->attempted = true;
    int result = tx->io.transfer(tx->io.context, &request, data);
    if (result) tx_error(tx, result);
    return !result;
}

static bool tx_read(void* context, unsigned short base, uint32_t sector,
                    void* data, bool master) {
    return !base && master && tx_io(context, REIST_STORAGE_JOURNAL_READ, sector, 1, data);
}
static bool tx_write_many(void* context, unsigned short base, uint32_t sector,
                          uint32_t count, const void* data, bool master) {
    return !base && master && tx_io(context, REIST_STORAGE_JOURNAL_WRITE_DEFERRED,
                                    sector, count, (void*)data);
}
static bool tx_write(void* context, unsigned short base, uint32_t sector,
                     const void* data, bool master) {
    return tx_write_many(context, base, sector, 1, data, master);
}
static bool tx_flush(void* context, unsigned short base, bool master) {
    return !base && master && tx_io(context, REIST_STORAGE_JOURNAL_FLUSH, 0, 0, NULL);
}
static bool tx_write_sync(void* context, unsigned short base, uint32_t sector,
                          const void* data, bool master) {
    return tx_write(context, base, sector, data, master) && tx_flush(context, base, master);
}
static bool tx_commit_begin(void* context, unsigned short base, bool master) {
    const reist_fat32_transaction_t* tx = context;
    return !base && master && tx->active && !tx->error;
}
static bool tx_commit_end(void* context, unsigned short base, bool master, bool commit) {
    return commit && tx_flush(context, base, master);
}
static const ata_journal_transport_t tx_transport = {
    .read = tx_read, .write = tx_write_sync, .commit_write = tx_write_sync,
    .write_deferred = tx_write, .write_sectors_deferred = tx_write_many,
    .flush = tx_flush, .commit_begin = tx_commit_begin,
    .commit_write_deferred = tx_write, .commit_write_sectors_deferred = tx_write_many,
    .commit_end = tx_commit_end
};

int reist_fat32_transaction_begin(reist_fat32_transaction_t* tx,
    const reist_fat32_transaction_io_t* io, const reist_file_object_key_t* key,
    uint32_t first, uint32_t sectors, uint16_t reserved, uint64_t deadline_ms) {
    if (!tx || !io || !io->guard || !io->transfer || !key ||
        key->kind != REIST_FILE_OBJECT_FAT32 || !sectors || sectors > UINT32_MAX - first ||
        reserved < ATA_JOURNAL_DATA_OFFSET + ATA_JOURNAL_MAX_ENTRIES || reserved > sectors ||
        !deadline_ms) return -REIST_EINVAL;
    if (tx->active) return -REIST_EBUSY;
    reist_file_object_guard_request_t request = {0};
    request.version = REIST_FILE_OBJECT_VERSION;
    request.struct_size = sizeof(request);
    request.operation = REIST_FILE_OBJECT_SNAPSHOT;
    int result = io->guard(io->context, &request);
    if (result) return result;
    request.operation = REIST_FILE_OBJECT_MUTATION_BEGIN;
    request.keys[0] = *key;
    request.flags = REIST_FILE_OBJECT_EXCLUSIVE | REIST_FILE_OBJECT_EXTERNAL_JOURNAL;
    request.deadline_ms = deadline_ms;
    result = io->guard(io->context, &request);
    if (result) return result;
    memset(tx, 0, sizeof(*tx));
    tx->io = *io;
    tx->resource = key->resource;
    tx->token = request.token;
    tx->first = first;
    tx->sectors = sectors;
    tx->active = true;
    ata_undo_journal_init(&tx->journal, &tx_transport, tx);
    if (!ata_undo_journal_attach(&tx->journal, 0, true, first, sectors, reserved) ||
        !tx->journal.enabled || !ata_undo_journal_transaction_begin(&tx->journal)) {
        tx_error(tx, -REIST_EIO);
        uint32_t outcome;
        return reist_fat32_transaction_finish(tx, false, &outcome);
    }
    return 0;
}

int reist_fat32_transaction_stage(reist_fat32_transaction_t* tx,
    uint32_t sector, const void* data) {
    if (!tx || !tx->active) return -REIST_ESTALE;
    if (tx->error) return tx->error;
    if (!data || sector < tx->first || sector - tx->first >= tx->sectors)
        return tx_error(tx, -REIST_EINVAL);
    if (!ata_undo_journal_write_sector(&tx->journal, 0, sector, data, true))
        return tx_error(tx, -REIST_EIO);
    return 0;
}

int reist_fat32_transaction_read(reist_fat32_transaction_t* tx,
    uint32_t sector, void* data) {
    if (!tx || !tx->active) return -REIST_ESTALE;
    if (tx->error) return tx->error;
    if (!data || sector < tx->first || sector - tx->first >= tx->sectors)
        return tx_error(tx, -REIST_EINVAL);
    if (!ata_undo_journal_read_sector(&tx->journal, 0, sector, data, true))
        return tx_error(tx, -REIST_EIO);
    return 0;
}

int reist_fat32_transaction_finish(reist_fat32_transaction_t* tx,
                                   bool commit, uint32_t* outcome) {
    if (!tx || !tx->active || !outcome) return -REIST_EINVAL;
    if (tx->journal.transaction_depth &&
        !ata_undo_journal_transaction_end(&tx->journal, commit && !tx->error))
        tx_error(tx, -REIST_EIO);
    *outcome = !tx->attempted ? REIST_FILE_OBJECT_NO_EFFECT :
        tx->error ? REIST_FILE_OBJECT_UNKNOWN : REIST_FILE_OBJECT_DURABLE_COMMIT;
    reist_file_object_guard_request_t request = {0};
    request.version = REIST_FILE_OBJECT_VERSION;
    request.struct_size = sizeof(request);
    request.operation = REIST_FILE_OBJECT_MUTATION_END;
    request.flags = *outcome;
    request.token = tx->token;
    int result = tx->io.guard(tx->io.context, &request);
    if (result) {
        tx_error(tx, result);
        /* Malformed/non-durable finish must not leave an invisible owner.
         * Explicit UNKNOWN is the only fallback; never reacquire or replay. */
        request.flags = *outcome = REIST_FILE_OBJECT_UNKNOWN;
        (void)tx->io.guard(tx->io.context, &request);
    }
    tx->active = false;
    tx->token = 0;
    return tx->error;
}
