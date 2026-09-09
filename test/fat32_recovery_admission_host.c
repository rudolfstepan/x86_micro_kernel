/* Actual portable journal, adversarial media and synchronous persistence cuts. */
#include "drivers/block/ata_journal.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define SECTORS 256U
#define PART 16U
#define VOLUME 200U
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __func__, __LINE__, #x); return false; } } while (0)
static uint8_t disk[SECTORS][512], initial[SECTORS][512], expected[SECTORS][512];
static ata_undo_journal_t journal, saved;
static unsigned reads, writes, flushes, fail_read, cut, checked, failures;
static bool power_off, cut_after;
static uint32_t trace[64];

static uint32_t crc32(const void *input, size_t n) {
    const uint8_t *p = input;
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static void put32(uint8_t *p, unsigned offset, uint32_t value) {
    memcpy(p + offset, &value, 4);
}
static void seal(uint8_t *header) {
    put32(header, 20, 0);
    put32(header, 20, crc32(header, 512));
}
static void mirror(void) { memcpy(disk[PART + 31], disk[PART + 8], 512); }
static bool read_sector(void *ctx, unsigned short base, uint32_t lba,
                        void *buffer, bool master) {
    (void)ctx; (void)base; (void)master;
    ++reads;
    if (power_off || lba >= SECTORS || lba == fail_read) return false;
    memcpy(buffer, disk[lba], 512);
    return true;
}
static bool write_sector(void *ctx, unsigned short base, uint32_t lba,
                         const void *buffer, bool master) {
    (void)ctx; (void)base; (void)master;
    if (power_off || lba >= SECTORS) return false;
    if (writes >= 64) return false;
    trace[writes++] = lba;
    if (writes == cut && !cut_after) { power_off = true; return false; }
    memcpy(disk[lba], buffer, 512);
    if (writes == cut) { power_off = true; return false; }
    return true;
}
static bool flush(void *ctx, unsigned short base, bool master) {
    (void)ctx; (void)base; (void)master;
    ++flushes;
    return !power_off;
}
static const ata_journal_transport_t transport = {
    .read = read_sector, .write = write_sector, .flush = flush
};
static void reset_runtime(void) {
    reads = writes = flushes = 0;
    fail_read = UINT_MAX; cut = 0; power_off = cut_after = false;
    ata_undo_journal_init(&journal, &transport, NULL);
}
static void fixture(unsigned count, bool redundant, bool v1) {
    memset(disk, 0x63, sizeof(disk));
    memset(disk[PART + 8], 0, 512);
    uint8_t *h = disk[PART + 8];
    put32(h, 0, ATA_JOURNAL_MAGIC);
    put32(h, 4, v1 ? 1 : 2);
    put32(h, 8, count ? ATA_JOURNAL_ACTIVE : ATA_JOURNAL_CLEAN);
    for (unsigned i = 0; i < count; ++i) {
        memset(disk[PART + 9 + i], (int)i + 1, 512);
        memset(disk[PART + 64 + i], (int)i + 0xa0, 512);
        if (!v1) {
            put32(h, 24 + i * 8, PART + 64 + i);
            put32(h, 28 + i * 8, crc32(disk[PART + 9 + i], 512));
        }
    }
    if (v1) {
        put32(h, 12, PART + 64);
        put32(h, 16, crc32(disk[PART + 9], 512));
        put32(h, 20, 7);
        put32(h, 24, crc32(h, 24));
    } else {
        put32(h, 12, 7); put32(h, 16, count); seal(h);
    }
    if (redundant && !v1) mirror();
    else memset(disk[PART + 31], 0, 512);
    reset_runtime();
}
static bool attach(unsigned reserved) {
    return ata_undo_journal_attach(&journal, 0x1f0, true, PART, VOLUME,
                                    (uint16_t)reserved);
}
static bool denied(unsigned reserved) {
    memcpy(initial, disk, sizeof(disk));
    CHECK(!attach(reserved));
    CHECK(!journal.enabled && writes == 0 && flushes == 0);
    CHECK(memcmp(initial, disk, sizeof(disk)) == 0);
    return true;
}
static bool malformed(unsigned kind, unsigned slot) {
    fixture(20, true, false);
    uint8_t *h = disk[PART + 8];
    switch (kind) {
    case 0: disk[PART + 9 + slot][13] ^= 1; break;
    case 1: fail_read = PART + 9 + slot; break;
    case 2: put32(h, 24, slot); seal(h); mirror(); break;
    case 3: put32(h, 32, PART + 64); seal(h); mirror(); break;
    case 4: h[20] ^= 1; mirror(); break;
    case 5: disk[PART + 31][100] ^= 1; seal(disk[PART + 31]); break;
    default: return false;
    }
    return denied(32);
}
static bool v1_target(unsigned target) {
    fixture(1, true, true);
    put32(disk[PART + 8], 12, target);
    put32(disk[PART + 8], 24, crc32(disk[PART + 8], 24));
    return denied(32);
}
static bool geometry(void) {
    const unsigned reservations[] = {0, 8, 9, 10, 28};
    for (unsigned i = 0; i < sizeof(reservations)/sizeof(reservations[0]); ++i) {
        fixture(0, false, false);
        memset(disk[PART + 8], 0, 512);
        CHECK(attach(reservations[i]));
        CHECK(!journal.enabled && writes == 0 && flushes == 0);
    }
    for (unsigned version = 0; version < 2; ++version) {
        for (unsigned reserved = 9; reserved < 29; ++reserved) {
            fixture(1, false, version != 0);
            CHECK(denied(reserved));
        }
    }
    const uint32_t claims[][3] = {
        {PART, 0, 32}, {PART, 31, 32}, {UINT32_MAX - 10, 32, 32},
        {UINT32_MAX - 8, 8, 32}
    };
    for (unsigned i = 0; i < sizeof(claims)/sizeof(claims[0]); ++i) {
        fixture(1, true, false);
        CHECK(!ata_undo_journal_attach(&journal, 0x1f0, true, claims[i][0],
                                        claims[i][1], (uint16_t)claims[i][2]));
        CHECK(reads == 0 && writes == 0 && flushes == 0);
    }
    for (unsigned which = 0; which < 2; ++which) {
        fixture(0, true, false);
        memset(disk[PART + 8], 0, 512); memset(disk[PART + 31], 0, 512);
        fail_read = PART + (which ? 31 : 8);
        CHECK(denied(32));
    }
    return true;
}
static bool v1_corruption(void) {
    for (unsigned kind = 0; kind < 4; ++kind) {
        fixture(1, true, true);
        if (kind == 0) disk[PART + 9][4] ^= 1;
        if (kind == 1) fail_read = PART + 9;
        if (kind == 2) disk[PART + 8][24] ^= 1;
        if (kind == 3) {
            put32(disk[PART + 8], 8, 2);
            put32(disk[PART + 8], 24, crc32(disk[PART + 8], 24));
        }
        CHECK(denied(32));
    }
    return true;
}
static bool owner_preserved(void) {
    fixture(0, true, false);
    CHECK(attach(32));
    CHECK(ata_undo_journal_transaction_begin(&journal));
    CHECK(ata_undo_journal_write_sector(&journal, 0x1f0, PART + 64,
                                        disk[0], true));
    memcpy(&saved, &journal, sizeof(saved));
    unsigned old_reads = reads, old_writes = writes;
    CHECK(!ata_undo_journal_attach(&journal, 0x170, false, 0, 128, 32));
    CHECK(memcmp(&saved, &journal, sizeof(saved)) == 0);
    CHECK(old_reads == reads && old_writes == writes && flushes == 0);
    CHECK(ata_undo_journal_transaction_end(&journal, true));
    CHECK(memcmp(disk[PART + 64], disk[0], 512) == 0);
    return true;
}
static bool recovery(unsigned count, bool redundant, bool v1) {
    fixture(count, redundant, v1);
    memcpy(initial, disk, sizeof(disk));
    memcpy(expected, disk, sizeof(disk));
    for (unsigned i = 0; i < count; ++i)
        memcpy(expected[PART + 64 + i], initial[PART + 9 + i], 512);
    ata_journal_record_t clean;
    ata_undo_journal_make_clean(&clean, 7);
    memcpy(expected[PART + 8], &clean, 512);
    if (redundant) memcpy(expected[PART + 31], &clean, 512);
    CHECK(attach(redundant ? 32 : 29));
    CHECK(journal.enabled && memcmp(expected, disk, sizeof(disk)) == 0);
    unsigned write_count = writes;
    for (unsigned i = 0; i < count; ++i)
        CHECK(trace[i] == PART + 64 + count - i - 1);
    for (unsigned after = 0; after < 2; ++after) {
        for (unsigned point = 1; point <= write_count; ++point) {
            memcpy(disk, initial, sizeof(disk)); reset_runtime();
            cut = point; cut_after = after != 0;
            CHECK(!attach(redundant ? 32 : 29));
            CHECK(!journal.enabled && writes == point);
            reset_runtime();
            CHECK(attach(redundant ? 32 : 29));
            CHECK(memcmp(expected, disk, sizeof(disk)) == 0);
            reset_runtime();
            CHECK(attach(redundant ? 32 : 29));
            CHECK(memcmp(expected, disk, sizeof(disk)) == 0);
        }
    }
    return true;
}
static bool header_repair(void) {
    for (unsigned bad = 0; bad < 2; ++bad) {
        fixture(2, true, false);
        fail_read = PART + (bad ? 31 : 8);
        CHECK(attach(32));
        CHECK(journal.enabled && writes == 4);
        CHECK(memcmp(disk[PART + 64], disk[PART + 9], 512) == 0);
        CHECK(memcmp(disk[PART + 65], disk[PART + 10], 512) == 0);
        CHECK(memcmp(disk[PART + 8], disk[PART + 31], 512) == 0);
    }
    return true;
}
static void record(bool passed) { ++checked; if (!passed) ++failures; }
int main(void) {
    for (unsigned i = 0; i < 20; ++i) {
        record(malformed(0, i)); record(malformed(1, i));
        record(malformed(2, PART + 9 + i)); record(v1_target(PART + 9 + i));
    }
    const unsigned targets[] = {PART - 1, PART + VOLUME, PART + 8, PART + 31, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
        record(malformed(2, targets[i])); record(v1_target(targets[i]));
    }
    record(malformed(3, 0)); record(malformed(4, 0)); record(malformed(5, 0));
    record(geometry()); record(owner_preserved()); record(header_repair());
    record(v1_corruption());
    record(recovery(20, true, false)); record(recovery(20, false, false));
    record(recovery(1, true, true)); record(recovery(1, false, true));
    record(recovery(0, true, false)); record(recovery(0, true, true));
    printf("FAT32_RECOVERY_HOST checked=%u failures=%u\n", checked, failures);
    return failures ? 1 : 0;
}
