/* Real Ring-3 journal terminal-state proof; reuse the established disk/IO
 * fixture, not a second implementation of the production transaction. */
#define main existing_ext2_namespace_main
#include "test_reist_vfs_symlink_host.c"
#undef main
#include <stdio.h>

#define JOURNAL_START (25U * BLOCK_SIZE)
#define JOURNAL_BYTES (26U * X86OS_STORAGE_BLOCK_SIZE)

static context_t committed, work, initial;
static uint8_t saved_image[sizeof(work.image)];

static uint32_t crc32(const uint8_t *data, uint32_t length) {
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static void checksum_header(uint8_t *header) {
    put32(header + 24U, 0U);
    put32(header + 24U, crc32(header, 512U));
}

static int stop_before_clean(void *opaque, uint32_t resource,
                             uint32_t sector, const uint8_t *data) {
    if (sector == JOURNAL_START / 512U &&
        read32(data) == 0x4B4E4C53U && read32(data + 8U) == 0U)
        return -5;
    return write_sector(opaque, resource, sector, data);
}

static void reset_io(context_t *ctx) {
    ctx->reads = ctx->writes = ctx->flushes = 0U;
    ctx->fail_write = ctx->fail_flush = UINT32_MAX;
}

static int recover(context_t *ctx) {
    reist_vfs_shadow_ext2_io_t io = io_for(ctx);
    return reist_vfs_shadow_ext2_recover_object(
        &io, 1U, ctx->now_ms + 10000U);
}

static int same_body(const context_t *left, const context_t *right) {
    return memcmp(left->image, right->image, JOURNAL_START) == 0 &&
        memcmp(left->image + JOURNAL_START + JOURNAL_BYTES,
               right->image + JOURNAL_START + JOURNAL_BYTES,
               sizeof(left->image) - JOURNAL_START - JOURNAL_BYTES) == 0;
}

static int refused_unchanged(void) {
    memcpy(saved_image, work.image, sizeof(saved_image));
    for (uint32_t pass = 0; pass < 2U; ++pass) {
        reset_io(&work);
        CHECK(recover(&work) == -5);
        CHECK(work.writes == 0U && work.flushes == 0U);
        CHECK(memcmp(saved_image, work.image, sizeof(saved_image)) == 0);
        CHECK(work.reads <= REIST_VFS_SHADOW_EXT2_MAX_TRANSACTION_READS);
    }
    return 0;
}

static int test_commit(void) {
    initialize(&initial);
    committed = initial;
    reist_vfs_shadow_ext2_io_t io = io_for(&committed);
    io.write_sector = stop_before_clean;
    const char *path = "/mnt/ext2/new-link";
    CHECK(reist_vfs_shadow_ext2_symlink(
        &io, "target.txt", 10U, path, (uint32_t)strlen(path),
        committed.now_ms + 10000U) == -5);
    const uint8_t *header = committed.image + JOURNAL_START;
    CHECK(read32(header) == 0x4B4E4C53U && read32(header + 8U) == 2U);
    CHECK(memcmp(header, header + 512U, 512U) == 0);
    const uint32_t count = read32(header + 16U);
    CHECK(count > 1U && count <= REIST_VFS_SHADOW_EXT2_MAX_JOURNAL_ENTRIES);

    /* Reintroduce each individually old target under genuine COMMITTED
     * headers. Each target was final and flushed before this corruption. */
    uint32_t changed = 0U;
    for (uint32_t entry = 0; entry < count; ++entry) {
        const uint8_t *record = header + 32U + entry * 16U;
        if (read32(record + 4U) == read32(record + 8U)) continue;
        ++changed;
        work = committed;
        memcpy(work.image + read32(record) * 512U,
               header + (entry + 2U) * 512U, 512U);
        CHECK(refused_unchanged() == 0);
    }
    CHECK(changed >= 2U);
    work = committed;
    for (uint32_t entry = 0; entry < count; ++entry)
        memcpy(work.image + read32(header + 32U + entry * 16U) * 512U,
               header + (entry + 2U) * 512U, 512U);
    CHECK(refused_unchanged() == 0);

    /* An invalid before-image and two valid but contradictory headers must
     * also refuse without trying cleanup or repair. */
    work = committed;
    work.image[JOURNAL_START + 1024U] ^= 1U;
    CHECK(refused_unchanged() == 0);
    work = committed;
    work.image[JOURNAL_START + 512U + 36U] ^= 1U;
    checksum_header(work.image + JOURNAL_START + 512U);
    CHECK(refused_unchanged() == 0);

    /* A real durable commit cleans only its two headers, then becomes a
     * zero-write no-op. One invalid redundant header can use the valid copy. */
    for (uint32_t bad_copy = 0; bad_copy < 3U; ++bad_copy) {
        work = committed;
        if (bad_copy < 2U) work.image[JOURNAL_START + bad_copy * 512U + 24U] ^= 1U;
        reset_io(&work);
        CHECK(recover(&work) == 0);
        CHECK(work.writes == 2U && work.flushes == 1U);
        CHECK(same_body(&work, &committed));
        CHECK(memcmp(work.image + JOURNAL_START + 1024U,
                     committed.image + JOURNAL_START + 1024U,
                     JOURNAL_BYTES - 1024U) == 0);
        CHECK(read32(work.image + JOURNAL_START + 8U) == 0U);
        reset_io(&work);
        CHECK(recover(&work) == 0 && work.writes == 0U && work.flushes == 0U);
    }

    /* Cut either CLEAN header write or its flush; a fresh recovery must
     * preserve all committed namespace/data bytes, never restore old ones. */
    for (uint32_t cut = 0; cut < 3U; ++cut) {
        work = committed;
        reset_io(&work);
        if (cut < 2U) work.fail_write = cut;
        else work.fail_flush = 0U;
        CHECK(recover(&work) == -5);
        CHECK(same_body(&work, &committed));
        reset_io(&work);
        CHECK(recover(&work) == 0);
        CHECK(same_body(&work, &committed));
        reset_io(&work);
        CHECK(recover(&work) == 0 && work.writes == 0U && work.flushes == 0U);
    }

    /* ACTIVE still owns undo authority even when all targets happen to be
     * final. This is deliberately different from COMMITTED. */
    work = committed;
    for (uint32_t copy = 0; copy < 2U; ++copy) {
        uint8_t *active = work.image + JOURNAL_START + copy * 512U;
        put32(active + 8U, 1U);
        checksum_header(active);
    }
    reset_io(&work);
    CHECK(recover(&work) == 0);
    CHECK(same_body(&work, &initial));
    reset_io(&work);
    CHECK(recover(&work) == 0 && work.writes == 0U);

    work = committed;
    reset_io(&work);
    io = io_for(&work);
    CHECK(reist_vfs_shadow_ext2_recover_object(&io, 1U, work.now_ms) == -110);
    CHECK(work.writes == 0U && work.flushes == 0U);
    printf("EXT2_COMMIT_HOST_OK targets=%u changed=%u cleanup_cuts=3\n", count, changed);
    return 0;
}

int main(void) {
    int result = test_commit();
    if (result != 0) fprintf(stderr, "EXT2_COMMIT_HOST_FAIL line=%d\n", result);
    return result == 0 ? 0 : 1;
}
