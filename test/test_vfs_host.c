/**
 * @file test/test_vfs_host.c
 * @brief Hostseitiger Regressionstest für vfs.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "fs/vfs/vfs.h"
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static int fake_mount(vfs_filesystem_t* fs, drive_t* drive) {
    (void)drive;
    vfs_node_t* root = (vfs_node_t*)calloc(1, sizeof(*root));
    if (!root) return VFS_ERR_NO_MEMORY;
    strcpy(root->name, "/");
    root->type = VFS_DIRECTORY;
    root->fs = fs;
    fs->root = root;
    return VFS_OK;
}

static int fake_unmount(vfs_filesystem_t* fs) {
    free(fs->root);
    fs->root = NULL;
    return VFS_OK;
}

static int fake_open(vfs_filesystem_t* fs, const char* path,
                     vfs_node_t** result) {
    if (!result) return VFS_ERR_INVALID;
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        *result = fs->root;
        return VFS_OK;
    }
    vfs_node_t* node = (vfs_node_t*)calloc(1, sizeof(*node));
    if (!node) return VFS_ERR_NO_MEMORY;
    strcpy(node->name, "file");
    node->type = VFS_FILE;
    node->fs = fs;
    *result = node;
    return VFS_OK;
}

static int fake_close(vfs_node_t* node) {
    if (!node || !node->fs) return VFS_ERR_INVALID;
    if (node != node->fs->root) free(node);
    return VFS_OK;
}

static int fake_truncate(vfs_node_t* node, uint32_t size) {
    if (!node) return VFS_ERR_INVALID;
    node->size = size;
    return VFS_OK;
}

static int fake_fstat(vfs_node_t* node, vfs_dir_entry_t* stat) {
    if (!node || !stat) return VFS_ERR_INVALID;
    memset(stat, 0, sizeof(*stat));
    strcpy(stat->name, node->name);
    stat->type = node->type;
    stat->size = node->size;
    stat->inode = 42U;
    return VFS_OK;
}

static vfs_filesystem_ops_t fake_ops = {
    .mount = fake_mount,
    .unmount = fake_unmount,
    .open = fake_open,
    .close = fake_close
};

int main(void) {
    drive_t root_drive = {0};
    drive_t nested_drive = {0};
    strcpy(root_drive.name, "root");
    strcpy(nested_drive.name, "data");

    vfs_init();
    CHECK(vfs_register_filesystem("fake", &fake_ops) == VFS_OK);
    CHECK(vfs_mount(&root_drive, "fake", "/") == VFS_OK);
    CHECK(vfs_mount(&nested_drive, "fake", "/mnt/data") == VFS_OK);
    CHECK(vfs_mount(&nested_drive, "fake", "/mnt/data") == VFS_ERR_EXISTS);
    CHECK(vfs_mount(&nested_drive, "fake", "/bad/") == VFS_ERR_INVALID);

    vfs_filesystem_t* root = vfs_get_filesystem("/mnt/database");
    vfs_filesystem_t* nested = vfs_get_filesystem("/mnt/data/file");
    CHECK(root != NULL && root->drive == &root_drive);
    CHECK(nested != NULL && nested->drive == &nested_drive);
    CHECK(vfs_get_filesystem("relative") == NULL);
    CHECK(vfs_get_filesystem("/mnt/data/../file") == NULL);
    CHECK(vfs_get_filesystem("/mnt//data") == NULL);

    vfs_node_t* node = NULL;
    CHECK(vfs_open("/mnt/data/file", &node) == VFS_OK);
    CHECK(node != NULL && nested->open_nodes == 1);
    node->size = 123U;
    CHECK(vfs_truncate(node, 1U) == VFS_ERR_UNSUPPORTED);
    CHECK(vfs_truncate(node, 0U) == VFS_ERR_UNSUPPORTED);
    vfs_dir_entry_t info;
    CHECK(vfs_fstat(node, &info) == VFS_ERR_UNSUPPORTED);
    fake_ops.truncate = fake_truncate;
    fake_ops.fstat = fake_fstat;
    CHECK(vfs_fstat(node, &info) == VFS_OK);
    CHECK(strcmp(info.name, "file") == 0 && info.type == VFS_FILE &&
          info.size == 123U && info.inode == 42U);
    CHECK(vfs_truncate(node, 77U) == VFS_OK && node->size == 77U);
    CHECK(vfs_truncate(node, 0U) == VFS_OK && node->size == 0U);
    CHECK(vfs_truncate(nested->root, 0U) == VFS_ERR_IS_DIR);
    CHECK(vfs_unmount("/mnt/data") == VFS_ERR_BUSY);
    CHECK(vfs_close(node) == VFS_OK);
    CHECK(nested->open_nodes == 0);
    CHECK(vfs_unmount("/mnt/data") == VFS_OK);
    CHECK(vfs_get_filesystem("/mnt/data/file") == root);
    CHECK(vfs_unmount("/") == VFS_OK);
    CHECK(vfs_get_filesystem("/anything") == NULL);
    return 0;
}
