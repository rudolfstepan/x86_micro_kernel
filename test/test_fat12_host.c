#include "fs/fat12/fat12.h"
#include <stdlib.h>
#include <string.h>

#define FLOPPY_SECTORS 2880u
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static uint8_t floppy[FLOPPY_SECTORS][FAT12_SECTOR_SIZE];
extern int get_next_cluster(int current_cluster);
extern fat12_t* fat12;

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
    return 0;
}
