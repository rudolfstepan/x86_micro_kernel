#include "fs/fat12/fat12.h"
#include "fs/vfs/vfs.h"
#include <stdlib.h>
#include <string.h>

#define FLOPPY_SECTORS 2880u
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint8_t floppy[FLOPPY_SECTORS][FAT12_SECTOR_SIZE];
extern int get_next_cluster(int current_cluster);
extern fat12_t* fat12;
extern vfs_filesystem_ops_t fat12_vfs_ops;

int vfs_register_filesystem(const char* name, vfs_filesystem_ops_t* ops) {
    (void)name;
    (void)ops;
    return VFS_OK;
}

bool fdc_read_sector(uint8_t drive, uint8_t head, uint8_t track,
                     uint8_t sector, void* output) {
    (void)drive;
    if (!output || head >= 2 || sector == 0 || sector > 18) return false;
    uint32_t logical = ((uint32_t)track * 2u + head) * 18u + sector - 1u;
    if (logical >= FLOPPY_SECTORS) return false;
    memcpy(output, floppy[logical], FAT12_SECTOR_SIZE);
    return true;
}

bool fdc_read_sectors(uint8_t drive, uint8_t head, uint8_t track,
                      uint8_t sector, uint8_t count, void* output) {
    if (!output || count == 0 || sector == 0 ||
        (uint16_t)sector + count - 1u > 18u) {
        return false;
    }

    uint8_t* destination = output;
    for (uint8_t index = 0; index < count; index++) {
        if (!fdc_read_sector(drive, head, track,
                             (uint8_t)(sector + index),
                             destination + (uint32_t)index * FAT12_SECTOR_SIZE)) {
            return false;
        }
    }
    return true;
}

bool fdd_write_sector(uint8_t drive, uint8_t head, uint8_t track,
                      uint8_t sector, const void* input) {
    (void)drive;
    if (!input || head >= 2 || sector == 0 || sector > 18) return false;
    uint32_t logical = ((uint32_t)track * 2u + head) * 18u + sector - 1u;
    if (logical >= FLOPPY_SECTORS) return false;
    memcpy(floppy[logical], input, FAT12_SECTOR_SIZE);
    return true;
}

bool fdc_write_sectors(uint8_t drive, uint8_t head, uint8_t track,
                       uint8_t sector, uint8_t count, const void* input) {
    if (!input || count == 0 || sector == 0 ||
        (uint16_t)sector + count - 1u > 18u) return false;
    const uint8_t* source = input;
    for (uint8_t index = 0; index < count; ++index) {
        if (!fdd_write_sector(drive, head, track,
                              (uint8_t)(sector + index),
                              source + (uint32_t)index * FAT12_SECTOR_SIZE))
            return false;
    }
    return true;
}

void hex_dump(const void* data, size_t size) {
    (void)data;
    (void)size;
}

static void set_fat_entry(uint8_t* fat, uint16_t cluster, uint16_t value) {
    uint32_t offset = ((uint32_t)cluster * 3u) / 2u;
    value &= 0x0FFF;
    if ((cluster & 1u) == 0) {
        fat[offset] = (uint8_t)value;
        fat[offset + 1] = (uint8_t)((fat[offset + 1] & 0xF0) |
                                    (value >> 8));
    } else {
        fat[offset] = (uint8_t)((fat[offset] & 0x0F) | (value << 4));
        fat[offset + 1] = (uint8_t)(value >> 4);
    }
}

static void set_short_name(directory_entry* entry, const char* base,
                           const char* extension) {
    memset(entry, 0, sizeof(*entry));
    memset(entry->filename, ' ', sizeof(entry->filename));
    memset(entry->extension, ' ', sizeof(entry->extension));
    memcpy(entry->filename, base, strlen(base));
    memcpy(entry->extension, extension, strlen(extension));
}

static void make_test_floppy(void) {
    memset(floppy, 0, sizeof(floppy));
    fat12_boot_sector boot;
    memset(&boot, 0, sizeof(boot));
    boot.bytes_per_sector = FAT12_SECTOR_SIZE;
    boot.sectors_per_cluster = 1;
    boot.reserved_sectors = 1;
    boot.fat_count = 2;
    boot.root_entry_count = FAT12_MAX_ROOT_ENTRIES;
    boot.total_sectors = FLOPPY_SECTORS;
    boot.media_descriptor = 0xF0;
    boot.sectors_per_fat = 9;
    boot.sectors_per_track = 18;
    boot.heads = 2;
    boot.boot_sector_signature = FAT12_BOOT_SIGNATURE;
    memcpy(floppy[0], &boot, sizeof(boot));

    uint8_t* fat = floppy[1];
    fat[0] = 0xF0;
    fat[1] = 0xFF;
    fat[2] = 0xFF;
    set_fat_entry(fat, 2, 3);
    set_fat_entry(fat, 3, FAT12_EOC_MAX);
    for (uint16_t cluster = 4; cluster < 18; cluster++)
        set_fat_entry(fat, cluster, cluster + 1);
    set_fat_entry(fat, 18, FAT12_EOC_MAX);
    set_fat_entry(fat, 19, FAT12_EOC_MAX);

    directory_entry* root = (directory_entry*)floppy[19];
    set_short_name(&root[0], "FILE", "BIN");
    root[0].first_cluster_low = 2;
    root[0].file_size = 700;
    set_short_name(&root[1], "SUB", "");
    root[1].attributes = FILE_ATTR_DIRECTORY;
    root[1].first_cluster_low = 4;

    for (uint32_t i = 0; i < FAT12_SECTOR_SIZE; i++) {
        floppy[33][i] = (uint8_t)i;
        floppy[34][i] = (uint8_t)(0x80u + (i & 0x3Fu));
    }

    uint32_t visible_index = 0;
    for (uint16_t cluster = 4; cluster <= 18; cluster++) {
        directory_entry* subdirectory =
            (directory_entry*)floppy[33 + cluster - 2];
        for (uint32_t index = 0; index < 16; index++, visible_index++) {
            if (visible_index == 0) {
                set_short_name(&subdirectory[index], ".", "");
                subdirectory[index].attributes = FILE_ATTR_DIRECTORY;
                subdirectory[index].first_cluster_low = 4;
            } else if (visible_index == 1) {
                set_short_name(&subdirectory[index], "..", "");
                subdirectory[index].attributes = FILE_ATTR_DIRECTORY;
            } else if (visible_index == 239) {
                set_short_name(&subdirectory[index], "INNER", "TXT");
                subdirectory[index].first_cluster_low = 19;
                subdirectory[index].file_size = 3;
            } else {
                char generated[5];
                generated[0] = 'E';
                generated[1] = (char)('0' + (visible_index / 100) % 10);
                generated[2] = (char)('0' + (visible_index / 10) % 10);
                generated[3] = (char)('0' + visible_index % 10);
                generated[4] = '\0';
                set_short_name(&subdirectory[index], generated, "");
            }
        }
    }
    memcpy(floppy[50], "yes", 3);
}

int main(void) {
    make_test_floppy();
    CHECK(floppy[1][3] == 3 && floppy[1][4] == 0xF0);
    CHECK(fat12_init_fs(0));
    CHECK(fat12->fat[3] == 3 && fat12->fat[4] == 0xF0);
    CHECK(get_next_cluster(2) == 3);
    fat12_file* file = fat12_open_file("file.bin", "r");
    CHECK(file != NULL && file->size == 700);
    file->position = 600;
    uint8_t slice[50];
    CHECK(fat12_read_file(file, slice, sizeof(slice), sizeof(slice)) == 50);
    for (uint32_t i = 0; i < sizeof(slice); i++) {
        CHECK(slice[i] == (uint8_t)(0x80u + ((88u + i) & 0x3Fu)));
    }
    fat12_close_file(file);

    CHECK(fat12_change_directory("sub"));
    file = fat12_open_file("INNER.TXT", "r");
    CHECK(file != NULL);
    char inner[4] = {0};
    CHECK(fat12_read_file(file, inner, sizeof(inner), 3) == 3);
    CHECK(memcmp(inner, "yes", 3) == 0);
    fat12_close_file(file);
    fat12_cleanup();

    vfs_filesystem_t fs;
    drive_t drive;
    memset(&fs, 0, sizeof(fs));
    memset(&drive, 0, sizeof(drive));
    fs.ops = &fat12_vfs_ops;
    drive.type = DRIVE_TYPE_FDD;
    drive.fdd_drive_no = 0;
    CHECK(fs.ops->mount(&fs, &drive) == VFS_OK);

    CHECK(fs.ops->create(&fs, "/NEW.TXT") == VFS_OK);
    vfs_node_t* node = NULL;
    CHECK(fs.ops->open(&fs, "/NEW.TXT", &node) == VFS_OK && node != NULL);
    uint8_t payload[900];
    for (uint32_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i * 17u + 3u);
    CHECK(fs.ops->write(node, 0, sizeof(payload), payload) ==
          (int)sizeof(payload));
    uint8_t verify[900];
    memset(verify, 0, sizeof(verify));
    CHECK(fs.ops->read(node, 0, sizeof(verify), verify) ==
          (int)sizeof(verify));
    CHECK(memcmp(payload, verify, sizeof(payload)) == 0);
    CHECK(fs.ops->close(node) == VFS_OK);

    CHECK(fs.ops->mkdir(&fs, "/EMPTY") == VFS_OK);
    CHECK(fs.ops->create(&fs, "/EMPTY/ITEM.BIN") == VFS_OK);
    CHECK(fs.ops->rmdir(&fs, "/EMPTY") == VFS_ERR_BUSY);
    CHECK(fs.ops->delete(&fs, "/EMPTY/ITEM.BIN") == VFS_OK);
    CHECK(fs.ops->rmdir(&fs, "/EMPTY") == VFS_OK);

    /* SUB initially fills 15 clusters. Creating another entry must extend
       its directory chain without corrupting INNER.TXT. */
    CHECK(fs.ops->create(&fs, "/SUB/EXTRA.BIN") == VFS_OK);
    CHECK(fs.ops->open(&fs, "/SUB/INNER.TXT", &node) == VFS_OK);
    memset(inner, 0, sizeof(inner));
    CHECK(fs.ops->read(node, 0, 3, (uint8_t*)inner) == 3);
    CHECK(memcmp(inner, "yes", 3) == 0);
    CHECK(fs.ops->close(node) == VFS_OK);
    CHECK(fs.ops->delete(&fs, "/NEW.TXT") == VFS_OK);
    CHECK(fs.ops->open(&fs, "/NEW.TXT", &node) == VFS_ERR_NOT_FOUND);

    /* Force the next two allocations to be non-contiguous and verify that
       normal file I/O follows the resulting fragmented chain. */
    CHECK(fat12_set_fat_entry(20, FAT12_EOC_MAX));
    CHECK(fat12_sync_fat());
    CHECK(fs.ops->create(&fs, "/FRAG.BIN") == VFS_OK);
    CHECK(fs.ops->open(&fs, "/FRAG.BIN", &node) == VFS_OK);
    CHECK(fs.ops->write(node, 0, sizeof(payload), payload) ==
          (int)sizeof(payload));
    CHECK(fat12_get_fat_entry((uint16_t)node->inode) != node->inode + 1U);
    memset(verify, 0, sizeof(verify));
    CHECK(fs.ops->read(node, 0, sizeof(verify), verify) ==
          (int)sizeof(verify));
    CHECK(memcmp(payload, verify, sizeof(payload)) == 0);
    CHECK(fs.ops->close(node) == VFS_OK);
    CHECK(fs.ops->delete(&fs, "/FRAG.BIN") == VFS_OK);

    /* A full medium must return NO-SPACE/IO without publishing a partial
       cluster chain or corrupting the empty directory entry. */
    for (uint32_t cluster = 2; cluster < fat12_cluster_count() + 2U;
         ++cluster) {
        if (fat12_get_fat_entry((uint16_t)cluster) == FAT12_FREE_CLUSTER)
            CHECK(fat12_set_fat_entry((uint16_t)cluster, FAT12_EOC_MAX));
    }
    CHECK(fat12_sync_fat());
    CHECK(fs.ops->create(&fs, "/FULL.BIN") == VFS_OK);
    CHECK(fs.ops->open(&fs, "/FULL.BIN", &node) == VFS_OK);
    CHECK(fs.ops->write(node, 0, 1, payload) == VFS_ERR_NO_SPACE);
    CHECK(node->inode == 0 && node->size == 0);
    CHECK(fs.ops->close(node) == VFS_OK);
    CHECK(memcmp(&floppy[1], &floppy[10], 9U * FAT12_SECTOR_SIZE) == 0);
    CHECK(fs.ops->unmount(&fs) == VFS_OK);
    return 0;
}
