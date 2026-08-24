/** Host behavior test for the bounded Ring-3 FAT32 stat parser. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_shadow_fat32.h"

#define TEST_SECTORS 4096U
#define TEST_RESERVED 32U
#define TEST_FAT_SECTORS 32U
#define TEST_DATA_START (TEST_RESERVED + 2U * TEST_FAT_SECTORS)

typedef struct {
    uint8_t image[TEST_SECTORS * X86OS_STORAGE_BLOCK_SIZE];
    uint32_t reads;
} test_context_t;

static void put16(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint8_t checksum(const uint8_t name[11]) {
    uint8_t value = 0U;
    for (uint32_t index = 0U; index < 11U; ++index) {
        value = (uint8_t)(((value & 1U) << 7U) |
                          ((value & 0xFEU) >> 1U));
        value = (uint8_t)(value + name[index]);
    }
    return value;
}

static void make_entry(uint8_t *entry, const char name[11], uint8_t attr,
                       uint32_t cluster, uint32_t size) {
    memset(entry, 0, 32U);
    memcpy(entry, name, 11U);
    entry[11U] = attr;
    put16(entry + 14U, 0U);
    put16(entry + 16U, 0x0021U);
    put16(entry + 18U, 0x0021U);
    put16(entry + 20U, cluster >> 16U);
    put16(entry + 22U, 0U);
    put16(entry + 24U, 0x0021U);
    put16(entry + 26U, cluster);
    put32(entry + 28U, size);
}

static void make_lfn(uint8_t *entry, const char *name,
                     const uint8_t short_name[11]) {
    static const uint8_t offsets[13] =
        {1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U};
    memset(entry, 0xFF, 32U);
    entry[0U] = 0x41U;
    entry[11U] = 0x0FU;
    entry[12U] = 0U;
    entry[13U] = checksum(short_name);
    put16(entry + 26U, 0U);
    uint32_t length = (uint32_t)strlen(name);
    for (uint32_t index = 0U; index < 13U; ++index) {
        uint16_t value = index < length ? (uint8_t)name[index] :
            (index == length ? 0U : 0xFFFFU);
        put16(entry + offsets[index], value);
    }
}

static void initialize(test_context_t *context) {
    memset(context, 0, sizeof(*context));
    uint8_t *boot = context->image;
    put16(boot + 11U, X86OS_STORAGE_BLOCK_SIZE);
    boot[13U] = 1U;
    put16(boot + 14U, TEST_RESERVED);
    boot[16U] = 2U;
    put32(boot + 32U, TEST_SECTORS);
    put32(boot + 36U, TEST_FAT_SECTORS);
    put32(boot + 44U, 2U);
    boot[510U] = 0x55U;
    boot[511U] = 0xAAU;

    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        uint8_t *fat = context->image +
            (TEST_RESERVED + copy * TEST_FAT_SECTORS) *
                X86OS_STORAGE_BLOCK_SIZE;
        put32(fat + 0U, 0x0FFFFFF8U);
        put32(fat + 4U, 0x0FFFFFFFU);
        for (uint32_t cluster = 2U; cluster <= 5U; ++cluster)
            put32(fat + cluster * 4U, 0x0FFFFFFFU);
    }

    uint8_t *root = context->image +
        TEST_DATA_START * X86OS_STORAGE_BLOCK_SIZE;
    static const char readme[11] = {'R','E','A','D','M','E',' ',' ',
                                    'T','X','T'};
    static const char usr[11] = {'U','S','R',' ',' ',' ',' ',' ',' ',' ',' '};
    static const uint8_t long_short[11] =
        {'L','O','N','G','N','A','~','1','T','X','T'};
    make_entry(root, readme, 0x20U, 3U, 7U);
    make_entry(root + 32U, usr, 0x10U, 4U, 0U);
    make_lfn(root + 64U, "Long Name.txt", long_short);
    make_entry(root + 96U, (const char *)long_short, 0x20U, 5U, 11U);

    uint8_t *nested = context->image +
        (TEST_DATA_START + 2U) * X86OS_STORAGE_BLOCK_SIZE;
    static const char app[11] = {'A','P','P',' ',' ',' ',' ',' ','P','R','G'};
    make_entry(nested, app, 0x20U, 5U, 1234U);
}

static int drive_info(void *opaque, uint32_t resource,
                      x86os_drive_info_t *info) {
    (void)opaque;
    memset(info, 0, sizeof(*info));
    if (resource > 1U) return 0;
    info->type = X86OS_DRIVE_PARTITION;
    info->sectors = TEST_SECTORS;
    strcpy(info->name, resource == 0U ? "hdd0p1" : "hdd0p2");
    strcpy(info->mount_point, resource == 0U ? "/" : "/mnt");
    return 1;
}

static int read_sector(void *opaque, uint32_t resource, uint32_t sector,
                       uint8_t data[X86OS_STORAGE_BLOCK_SIZE]) {
    test_context_t *context = opaque;
    if (resource > 1U || sector >= TEST_SECTORS) return -1;
    ++context->reads;
    memcpy(data, context->image + sector * X86OS_STORAGE_BLOCK_SIZE,
           X86OS_STORAGE_BLOCK_SIZE);
    return 0;
}

static int stat_path(test_context_t *context, const char *path,
                     x86os_file_info_t *info) {
    const reist_vfs_shadow_io_t io = {
        .context = context,
        .drive_info = drive_info,
        .read_sector = read_sector,
    };
    context->reads = 0U;
    return reist_vfs_shadow_fat32_stat(
        &io, path, (uint32_t)strlen(path), info);
}

int main(void) {
    static test_context_t context;
    x86os_file_info_t info;
    initialize(&context);
    if (stat_path(&context, "/", &info) != 0 ||
        strcmp(info.name, "/") != 0 || info.type != X86OS_DIRECTORY)
        return 1;
    if (stat_path(&context, "/readme.txt", &info) != 0 ||
        strcmp(info.name, "README.TXT") != 0 || info.type != X86OS_FILE ||
        info.size != 7U || info.create_time != 315532800U ||
        context.reads > REIST_VFS_SHADOW_MAX_SECTOR_READS) return 2;
    if (stat_path(&context, "/USR/APP.PRG", &info) != 0 ||
        strcmp(info.name, "APP.PRG") != 0 || info.size != 1234U) return 3;
    if (stat_path(&context, "/Long Name.txt", &info) != 0 ||
        strcmp(info.name, "Long Name.txt") != 0 || info.size != 11U) return 4;
    if (stat_path(&context, "/mnt/README.TXT", &info) != 0 ||
        info.size != 7U) return 5;
    if (stat_path(&context, "/missing", &info) != -2) return 6;
    if (stat_path(&context, "/../README.TXT", &info) != -22) return 7;
    context.image[510U] = 0U;
    if (stat_path(&context, "/README.TXT", &info) != -2) return 8;
    return 0;
}
