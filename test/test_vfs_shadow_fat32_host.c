/** Host behavior test for the bounded Ring-3 FAT12/FAT32 stat parser. */
#include <stdint.h>
#include <string.h>

#include "userspace/storage/include/reist/vfs_shadow_fat32.h"

#define TEST_SECTORS 70000U
#define TEST_RESERVED 32U
#define TEST_FAT_SECTORS 600U
#define TEST_DATA_START (TEST_RESERVED + 2U * TEST_FAT_SECTORS)
#define FAT12_SECTORS 2880U
#define FAT12_ROOT_START 19U
#define FAT12_DATA_START 33U

typedef struct {
    uint8_t image[TEST_SECTORS * X86OS_STORAGE_BLOCK_SIZE];
    uint32_t reads;
    uint32_t sectors;
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

static void initialize_fat32(test_context_t *context) {
    memset(context, 0, sizeof(*context));
    context->sectors = TEST_SECTORS;
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
        for (uint32_t cluster = 2U; cluster <= 7U; ++cluster)
            put32(fat + cluster * 4U, 0x0FFFFFFFU);
        put32(fat + 6U * 4U, 7U);
    }

    uint8_t *root = context->image +
        TEST_DATA_START * X86OS_STORAGE_BLOCK_SIZE;
    static const char readme[11] = {'R','E','A','D','M','E',' ',' ',
                                    'T','X','T'};
    static const char usr[11] = {'U','S','R',' ',' ',' ',' ',' ',' ',' ',' '};
    static const uint8_t long_short[11] =
        {'L','O','N','G','N','A','~','1','T','X','T'};
    static const char cross[11] = {'C','R','O','S','S',' ',' ',' ',
                                   'B','I','N'};
    make_entry(root, readme, 0x20U, 3U, 7U);
    make_entry(root + 32U, usr, 0x10U, 4U, 0U);
    make_lfn(root + 64U, "Long Name.txt", long_short);
    make_entry(root + 96U, (const char *)long_short, 0x20U, 5U, 11U);
    make_entry(root + 128U, cross, 0x20U, 6U, 700U);

    uint8_t *cross_first = context->image +
        (TEST_DATA_START + 4U) * X86OS_STORAGE_BLOCK_SIZE;
    uint8_t *cross_second = context->image +
        (TEST_DATA_START + 5U) * X86OS_STORAGE_BLOCK_SIZE;
    memset(cross_first, 'A', X86OS_STORAGE_BLOCK_SIZE);
    memset(cross_second, 'B', X86OS_STORAGE_BLOCK_SIZE);

    uint8_t *nested = context->image +
        (TEST_DATA_START + 2U) * X86OS_STORAGE_BLOCK_SIZE;
    static const char app[11] = {'A','P','P',' ',' ',' ',' ',' ','P','R','G'};
    make_entry(nested, app, 0x20U, 5U, 1234U);
}

static void set_fat12_entry(uint8_t *fat, uint32_t cluster,
                            uint16_t value) {
    uint32_t offset = cluster + cluster / 2U;
    value &= 0x0FFFU;
    if ((cluster & 1U) == 0U) {
        fat[offset] = (uint8_t)value;
        fat[offset + 1U] = (uint8_t)((fat[offset + 1U] & 0xF0U) |
                                     (value >> 8U));
    } else {
        fat[offset] = (uint8_t)((fat[offset] & 0x0FU) | (value << 4U));
        fat[offset + 1U] = (uint8_t)(value >> 4U);
    }
}

static void initialize_fat12(test_context_t *context) {
    memset(context, 0, sizeof(*context));
    context->sectors = FAT12_SECTORS;
    uint8_t *boot = context->image;
    put16(boot + 11U, X86OS_STORAGE_BLOCK_SIZE);
    boot[13U] = 1U;
    put16(boot + 14U, 1U);
    boot[16U] = 2U;
    put16(boot + 17U, 224U);
    put16(boot + 19U, FAT12_SECTORS);
    boot[21U] = 0xF0U;
    put16(boot + 22U, 9U);
    boot[510U] = 0x55U;
    boot[511U] = 0xAAU;

    for (uint32_t copy = 0U; copy < 2U; ++copy) {
        uint8_t *fat = context->image + (1U + copy * 9U) *
            X86OS_STORAGE_BLOCK_SIZE;
        fat[0U] = 0xF0U;
        fat[1U] = 0xFFU;
        fat[2U] = 0xFFU;
        set_fat12_entry(fat, 2U, 0x0FFFU);
        set_fat12_entry(fat, 3U, 0x0FFFU);
        set_fat12_entry(fat, 4U, 0x0FFFU);
        set_fat12_entry(fat, 5U, 0x0FF7U);
        set_fat12_entry(fat, 341U, 342U);
        set_fat12_entry(fat, 342U, 0x0FFFU);
    }

    uint8_t *root = context->image +
        FAT12_ROOT_START * X86OS_STORAGE_BLOCK_SIZE;
    static const char hotplug[11] =
        {'H','O','T','P','L','U','G',' ','T','X','T'};
    static const char dir[11] =
        {'D','I','R',' ',' ',' ',' ',' ',' ',' ',' '};
    static const char bad[11] =
        {'B','A','D',' ',' ',' ',' ',' ',' ',' ',' '};
    static const char boundary[11] =
        {'B','O','U','N','D',' ',' ',' ',' ',' ',' '};
    static const uint8_t long_short[11] =
        {'L','O','N','G','F','A','~','1','T','X','T'};
    make_entry(root, hotplug, 0x20U, 2U, 14U);
    make_entry(root + 32U, dir, 0x10U, 3U, 0U);
    make_lfn(root + 64U, "Fat12 Long", long_short);
    make_entry(root + 96U, (const char *)long_short, 0x20U, 4U, 17U);
    make_entry(root + 128U, bad, 0x10U, 5U, 0U);
    make_entry(root + 160U, boundary, 0x10U, 341U, 0U);

    uint8_t *nested = context->image +
        (FAT12_DATA_START + 1U) * X86OS_STORAGE_BLOCK_SIZE;
    static const char file[11] =
        {'F','I','L','E',' ',' ',' ',' ','B','I','N'};
    make_entry(nested, file, 0x20U, 4U, 99U);

    uint8_t *bad_cluster = context->image +
        (FAT12_DATA_START + 3U) * X86OS_STORAGE_BLOCK_SIZE;
    memset(bad_cluster, 0xE5, X86OS_STORAGE_BLOCK_SIZE);
    uint8_t *boundary_first = context->image +
        (FAT12_DATA_START + 339U) * X86OS_STORAGE_BLOCK_SIZE;
    memset(boundary_first, 0xE5, X86OS_STORAGE_BLOCK_SIZE);
    uint8_t *boundary_second = context->image +
        (FAT12_DATA_START + 340U) * X86OS_STORAGE_BLOCK_SIZE;
    static const char edge[11] =
        {'E','D','G','E',' ',' ',' ',' ','T','X','T'};
    make_entry(boundary_second, edge, 0x20U, 2U, 21U);
}

static int drive_info(void *opaque, uint32_t resource,
                      x86os_drive_info_t *info) {
    test_context_t *context = opaque;
    memset(info, 0, sizeof(*info));
    if (resource > 1U) return 0;
    info->type = X86OS_DRIVE_PARTITION;
    info->sectors = context->sectors;
    strcpy(info->name, resource == 0U ? "hdd0p1" : "hdd0p2");
    strcpy(info->mount_point, resource == 0U ? "/" : "/mnt");
    return 1;
}

static int read_sector(void *opaque, uint32_t resource, uint32_t sector,
                       uint8_t data[X86OS_STORAGE_BLOCK_SIZE]) {
    test_context_t *context = opaque;
    if (resource > 1U || sector >= context->sectors) return -1;
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
    return reist_vfs_shadow_fat_stat(
        &io, path, (uint32_t)strlen(path), info);
}

static int fat32_stat_path(test_context_t *context, const char *path,
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

static int read_path(test_context_t *context, const char *path,
                     uint32_t offset, uint8_t *data, uint32_t capacity,
                     uint32_t *transferred) {
    const reist_vfs_shadow_io_t io = {context, drive_info, read_sector};
    context->reads = 0U;
    return reist_vfs_shadow_fat_read(
        &io, path, (uint32_t)strlen(path), offset, data, capacity, transferred);
}

static int readdir_path(test_context_t *context, const char *path,
                        uint32_t index, x86os_file_info_t *info) {
    const reist_vfs_shadow_io_t io = {context, drive_info, read_sector};
    context->reads = 0U;
    return reist_vfs_shadow_fat_readdir(
        &io, path, (uint32_t)strlen(path), index, info);
}

int main(void) {
    static test_context_t context;
    x86os_file_info_t info;
    initialize_fat32(&context);
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
    uint8_t data[32];
    uint32_t transferred = 0U;
    if (read_path(&context, "/CROSS.BIN", 500U, data, sizeof(data),
                  &transferred) != 0 || transferred != sizeof(data) ||
        data[0U] != 'A' || data[11U] != 'A' || data[12U] != 'B' ||
        data[31U] != 'B' ||
        context.reads > REIST_VFS_SHADOW_MAX_SECTOR_READS) return 16;
    if (read_path(&context, "/CROSS.BIN", 700U, data, sizeof(data),
                  &transferred) != 0 || transferred != 0U) return 17;
    if (read_path(&context, "/USR", 0U, data, sizeof(data),
                  &transferred) != -21) return 18;
    if (readdir_path(&context, "/", 0U, &info) != 0 ||
        strcmp(info.name, "README.TXT") != 0) return 19;
    if (readdir_path(&context, "/", 2U, &info) != 0 ||
        strcmp(info.name, "Long Name.txt") != 0) return 20;
    if (readdir_path(&context, "/", 4U, &info) != 1 ||
        info.name[0U] != '\0') return 21;
    if (readdir_path(&context, "/README.TXT", 0U, &info) != -20) return 22;
    const reist_vfs_shadow_io_t object_io = {
        &context, drive_info, read_sector
    };
    reist_vfs_shadow_object_t object;
    if (reist_vfs_shadow_fat_object_open(
            &object_io, "/CROSS.BIN", 10U, &object, &info) != 0 ||
        object.filesystem != REIST_VFS_SHADOW_OBJECT_FAT ||
        object.locator_a != TEST_DATA_START || object.locator_b != 128U)
        return 23;
    uint8_t *cross_entry = context.image +
        TEST_DATA_START * X86OS_STORAGE_BLOCK_SIZE + 128U;
    static const char moved[11] = {'M','O','V','E','D',' ',' ',' ',
                                   'B','I','N'};
    memcpy(cross_entry, moved, sizeof(moved));
    if (reist_vfs_shadow_fat_object_read(
            &object_io, &object, 500U, data, sizeof(data), &transferred) != 0 ||
        transferred != sizeof(data) || data[12U] != 'B' ||
        stat_path(&context, "/CROSS.BIN", &info) != -2 ||
        stat_path(&context, "/MOVED.BIN", &info) != 0)
        return 24;
    context.image[3U] ^= 0x5AU;
    if (reist_vfs_shadow_fat_object_read(
            &object_io, &object, 0U, data, sizeof(data), &transferred) != -116)
        return 25;
    initialize_fat32(&context);
    if (reist_vfs_shadow_fat_object_open(
            &object_io, "/README.TXT", 11U, &object, &info) != 0)
        return 26;
    context.image[TEST_DATA_START * X86OS_STORAGE_BLOCK_SIZE + 14U] ^= 1U;
    if (reist_vfs_shadow_fat_object_stat(&object_io, &object, &info) != -116)
        return 27;
    initialize_fat32(&context);
    if (stat_path(&context, "/../README.TXT", &info) != -22) return 7;
    context.image[510U] = 0U;
    if (stat_path(&context, "/README.TXT", &info) != -2) return 8;
    initialize_fat12(&context);
    if (stat_path(&context, "/HOTPLUG.TXT", &info) != 0 ||
        strcmp(info.name, "HOTPLUG.TXT") != 0 || info.size != 14U ||
        context.reads > REIST_VFS_SHADOW_MAX_SECTOR_READS) return 9;
    if (stat_path(&context, "/DIR/FILE.BIN", &info) != 0 ||
        info.size != 99U) return 10;
    if (stat_path(&context, "/Fat12 Long", &info) != 0 ||
        strcmp(info.name, "Fat12 Long") != 0 || info.size != 17U)
        return 11;
    if (stat_path(&context, "/BOUND/EDGE.TXT", &info) != 0 ||
        info.size != 21U) return 12;
    if (stat_path(&context, "/BAD/MISSING.TXT", &info) != -5) return 13;
    if (fat32_stat_path(&context, "/HOTPLUG.TXT", &info) != -2) return 14;
    put16(context.image + 17U, 5000U);
    if (stat_path(&context, "/HOTPLUG.TXT", &info) != -2) return 15;
    return 0;
}
