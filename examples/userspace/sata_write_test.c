#include <stdint.h>

#include "x86os.h"

#define SATA_WRITE_PATH "/SATAWR.TST"
#define SATA_POST_PATH "/SATAPST.TST"
#define SATA_WRITE_MAGIC 0x53575431U
#define SATA_WRITE_DURATION_MS 10000U
#define SATA_RECONNECT_TIMEOUT_MS 30000U
#define SATA_WRITE_MAX_RECORDS 2048U
#define SATA_RECORD_SIZE 512U
#define SATA_PAYLOAD_SIZE 492U
#define SATA_MAX_DRIVES 32U

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint64_t timestamp_ms;
    uint32_t crc32;
    uint8_t payload[SATA_PAYLOAD_SIZE];
} sata_record_t;

_Static_assert(sizeof(sata_record_t) == SATA_RECORD_SIZE,
               "SATA test record must be one sector");

static uint32_t record_crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static void prepare_record(sata_record_t *record, uint32_t sequence,
                           uint64_t timestamp_ms) {
    record->magic = SATA_WRITE_MAGIC;
    record->sequence = sequence;
    record->timestamp_ms = timestamp_ms;
    record->crc32 = 0U;
    for (uint32_t index = 0U; index < SATA_PAYLOAD_SIZE; ++index)
        record->payload[index] = (uint8_t)(sequence * 131U + index * 17U);
    record->crc32 = record_crc32(record, sizeof(*record));
}

static int valid_record(sata_record_t *record, uint32_t sequence) {
    if (record->magic != SATA_WRITE_MAGIC || record->sequence != sequence)
        return 0;
    uint32_t expected = record->crc32;
    record->crc32 = 0U;
    uint32_t actual = record_crc32(record, sizeof(*record));
    record->crc32 = expected;
    return expected == actual;
}

static void print_count(const char *prefix, uint32_t value) {
    x86os_puts(prefix);
    x86os_print_number((int)value);
    x86os_puts("\n");
}

static int is_root_disk(void) {
    for (uint32_t index = 0U; index < SATA_MAX_DRIVES; ++index) {
        x86os_drive_info_t drive;
        int result = x86os_drive_info(index, &drive);
        if (result == 0) break;
        if (result < 0 || drive.mount_point[0] != '/' ||
            drive.mount_point[1] != '\0') continue;
        return drive.type == X86OS_DRIVE_ATA ||
               drive.type == X86OS_DRIVE_AHCI ||
               drive.type == X86OS_DRIVE_PARTITION;
    }
    return 0;
}

static int partition_available(void) {
    x86os_file_info_t info;
    uint8_t magic[4];
    if (x86os_stat("/SHELL.PRG", &info) != 0 || info.type != X86OS_FILE)
        return 0;
    int descriptor = x86os_open("/SHELL.PRG");
    if (descriptor < 0) return 0;
    int amount = x86os_read(descriptor, magic, sizeof(magic));
    int closed = x86os_close(descriptor);
    return amount == (int)sizeof(magic) && closed == 0 &&
           magic[0] == 'M' && magic[1] == 'Y' &&
           magic[2] == 'P' && magic[3] == 'R';
}

static int wait_for_reconnect(void) {
    uint64_t start = 0U;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&start) != 0) return -1;
    do {
        if (partition_available()) return 0;
        if (x86os_sleep_ms(250U) != 0) return -1;
        if (x86os_monotonic_ms(&now) != 0) return -1;
    } while (now - start < SATA_RECONNECT_TIMEOUT_MS);
    return -1;
}

static int verify_file(uint32_t *records_out) {
    int descriptor = x86os_open(SATA_WRITE_PATH);
    if (descriptor < 0) {
        *records_out = 0U;
        return 1; /* Complete rollback of file creation is an old state. */
    }
    sata_record_t record;
    uint32_t sequence = 0U;
    int result = 0;
    for (;;) {
        int amount = x86os_read(descriptor, &record, sizeof(record));
        if (amount == 0) break;
        if (amount != (int)sizeof(record) || sequence >= SATA_WRITE_MAX_RECORDS ||
            !valid_record(&record, sequence)) {
            result = -1;
            break;
        }
        ++sequence;
    }
    if (x86os_close(descriptor) < 0) result = -1;
    *records_out = sequence;
    return result;
}

static int verify_recovered_write(void) {
    sata_record_t written;
    sata_record_t readback;
    prepare_record(&written, 0U, 0U);
    (void)x86os_unlink(SATA_POST_PATH);
    int descriptor = x86os_create(SATA_POST_PATH);
    if (descriptor < 0) return -1;
    int write_result = x86os_write(descriptor, &written, sizeof(written));
    int sync_result = write_result == (int)sizeof(written)
        ? x86os_fsync(descriptor) : -1;
    int close_result = x86os_close(descriptor);
    if (write_result != (int)sizeof(written) || sync_result != 0 ||
        close_result != 0) return -1;
    descriptor = x86os_open(SATA_POST_PATH);
    if (descriptor < 0) return -1;
    int read_result = x86os_read(descriptor, &readback, sizeof(readback));
    close_result = x86os_close(descriptor);
    int valid = read_result == (int)sizeof(readback) && close_result == 0 &&
                valid_record(&readback, 0U);
    if (valid) (void)x86os_unlink(SATA_POST_PATH);
    return valid ? 0 : -1;
}

int main(void) {
    if (!is_root_disk()) {
        x86os_puts("SATA_WRITE TEST_FAIL C: is not a disk volume\n");
        return 1;
    }
    x86os_puts("SATA_WRITE READY: disconnect and reconnect SATA during ACTIVE\n");
    x86os_puts("SATA_WRITE path=" SATA_WRITE_PATH " duration_ms=10000\n");
    (void)x86os_unlink(SATA_WRITE_PATH);
    int descriptor = x86os_create(SATA_WRITE_PATH);
    if (descriptor < 0) {
        x86os_puts("SATA_WRITE TEST_FAIL create\n");
        return 1;
    }

    uint64_t start = 0U;
    uint64_t now = 0U;
    if (x86os_monotonic_ms(&start) != 0) {
        (void)x86os_close(descriptor);
        x86os_puts("SATA_WRITE TEST_FAIL clock\n");
        return 1;
    }
    x86os_puts("SATA_WRITE ACTIVE\n");
    uint32_t sequence = 0U;
    int io_failed = 0;
    while (sequence < SATA_WRITE_MAX_RECORDS) {
        if (x86os_monotonic_ms(&now) != 0 ||
            now - start >= SATA_WRITE_DURATION_MS) break;
        sata_record_t record;
        prepare_record(&record, sequence, now);
        if (x86os_write(descriptor, &record, sizeof(record)) !=
                (int)sizeof(record) || x86os_fsync(descriptor) != 0) {
            io_failed = 1;
            break;
        }
        ++sequence;
        if (x86os_sleep_ms(5U) != 0) {
            io_failed = 1;
            break;
        }
    }
    int close_result = x86os_close(descriptor);
    print_count("SATA_WRITE records_attempted=", sequence);
    if (io_failed || close_result < 0)
        x86os_puts("SATA_WRITE IO_ERROR_DETECTED\n");
    else
        x86os_puts("SATA_WRITE WINDOW_COMPLETE\n");

    x86os_puts("SATA_WRITE WAIT_RECONNECT\n");
    if (wait_for_reconnect() != 0) {
        x86os_puts("SATA_WRITE TEST_FAIL reconnect_timeout\n");
        return 2;
    }
    x86os_puts("SATA_WRITE RECONNECTED PARTITION_OK\n");

    uint32_t verified = 0U;
    int verification = verify_file(&verified);
    if (verification < 0) {
        print_count("SATA_WRITE corrupt_after_record=", verified);
        x86os_puts("SATA_WRITE TEST_FAIL file_corrupt\n");
        return 3;
    }
    if (verification > 0)
        x86os_puts("SATA_WRITE FILE_ROLLED_BACK_OLD_STATE\n");
    else
        print_count("SATA_WRITE FILE_PREFIX_OK records=", verified);
    if (verify_recovered_write() != 0) {
        x86os_puts("SATA_WRITE TEST_FAIL recovery_not_writable\n");
        return 4;
    }
    x86os_puts("SATA_WRITE RECOVERY_RW_OK\n");
    x86os_puts("SATA_WRITE TEST_OK\n");
    return 0;
}
