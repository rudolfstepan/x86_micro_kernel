/**
 * @file kernel/init/admin_maintenance.c
 * @brief Verarbeitet autorisierte, generationsgebundene Wartungstransaktionen.
 *
 * Layer: Ring-0 administration service.
 * Contract: Ressourcenreservierung und Cleanup folgen einer festen Zustandsmaschine.
 * Safety: Kapazität, Rechte und Deadline werden vor Seiteneffekten geprüft.
 */
#include "include/kernel/admin_maintenance.h"

#include <stddef.h>

#include "arch/x86/include/interrupt.h"
#include "drivers/block/ata.h"
#include "drivers/block/block_device.h"
#include "drivers/bus/drives.h"
#include "fs/vfs/vfs.h"
#include "include/kernel/critical_object.h"
#include "include/kernel/storage_maintenance.h"
#include "include/kernel/storage_service.h"
#include "kernel/proc/process.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#include "lib/libc/string.h"

#define ADMIN_CONTROL_VERSION 1U
#define ADMIN_RESOURCE_VERSION 1U
#define ADMIN_DRAIN_POLL_MS 10U
#define ADMIN_EINVAL (-22)
#define ADMIN_EBUSY (-16)
#define ADMIN_EACCES (-13)
#define ADMIN_EIO (-5)
#define ADMIN_ENOENT (-2)
#define ADMIN_ETIMEDOUT (-110)
#define ADMIN_EINTEGRITY (-84)

typedef struct {
    uint32_t active;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t command;
    uint32_t target;
    uint32_t resource_mask;
    uint32_t transaction_generation;
    uint32_t root_resource_mask;
    uint32_t reserved;
    uint64_t deadline_ms;
} admin_control_t;

typedef struct {
    uint32_t generation;
    uint32_t saved_mount;
    uint32_t saved_fs_kind;
    uint32_t saved_crc32;
    uint32_t reserved[2];
} admin_resource_meta_t;

typedef struct {
    char path[ADMIN_MAINTENANCE_PATH_MAX];
    char fs_type[ADMIN_MAINTENANCE_FS_MAX];
} admin_saved_mount_t;

static critical_object_t protected_control;
static critical_object_t protected_resources[MAX_DRIVES];
static admin_saved_mount_t saved_mounts[MAX_DRIVES];
static storage_maintenance_token_t active_tokens[MAX_DRIVES];
static uint32_t active_fingerprints[MAX_DRIVES];
static bool initialized;

_Static_assert(MAX_DRIVES > 0 && MAX_DRIVES < 32,
               "admin maintenance requires a 1..31 resource mask");
_Static_assert(sizeof(admin_control_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "admin control exceeds protected object capacity");
_Static_assert(sizeof(admin_resource_meta_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "admin resource metadata exceeds protected object capacity");
_Static_assert(sizeof(admin_storage_request_t) == 104U,
               "admin request ABI size changed");
_Static_assert(sizeof(admin_storage_result_t) == 128U,
               "admin result ABI size changed");

static bool control_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(admin_control_t)) return false;
    const admin_control_t *control = payload;
    uint32_t valid_resources = (1U << MAX_DRIVES) - 1U;
    if (control->active > 1U || control->transaction_generation == 0U ||
        control->root_resource_mask == 0U ||
        (control->root_resource_mask & ~valid_resources) != 0U ||
        control->reserved != 0U) return false;
    if (control->active == 0U) {
        return control->owner_pid == 0 &&
               control->owner_generation == 0U && control->command == 0U &&
               control->target == 0U && control->resource_mask == 0U &&
               control->deadline_ms == 0U;
    }
    return control->owner_pid > 0 && control->owner_generation != 0U &&
           control->command >= ADMIN_STORAGE_DEVICE_DOWN &&
           control->command <= ADMIN_STORAGE_UMOUNT &&
           control->target < MAX_DRIVES && control->resource_mask != 0U &&
           (control->resource_mask & ~((1U << MAX_DRIVES) - 1U)) == 0U &&
           control->deadline_ms != 0U;
}

static bool resource_meta_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(admin_resource_meta_t))
        return false;
    const admin_resource_meta_t *meta = payload;
    if (meta->generation == 0U || meta->saved_mount > 1U ||
        meta->saved_fs_kind > 3U || meta->reserved[0] != 0U ||
        meta->reserved[1] != 0U) return false;
    return meta->saved_mount != 0U
        ? meta->saved_fs_kind != 0U && meta->saved_crc32 != 0U
        : meta->saved_fs_kind == 0U && meta->saved_crc32 == 0U;
}

static uint32_t text_crc32(const char *first, size_t first_capacity,
                           const char *second, size_t second_capacity) {
    uint32_t crc = 0xFFFFFFFFU;
    const char *parts[2] = {first, second};
    size_t capacities[2] = {first_capacity, second_capacity};
    for (uint32_t part = 0U; part < 2U; ++part) {
        for (size_t index = 0U; index < capacities[part]; ++index) {
            uint8_t value = (uint8_t)parts[part][index];
            crc ^= value;
            for (uint32_t bit = 0U; bit < 8U; ++bit)
                crc = (crc >> 1) ^
                    (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

static bool bounded_text(const char *text, size_t capacity,
                         size_t *length_out) {
    if (text == NULL || capacity == 0U) return false;
    for (size_t length = 0U; length < capacity; ++length) {
        if (text[length] != '\0') continue;
        if (length_out != NULL) *length_out = length;
        return true;
    }
    return false;
}

static uint32_t fs_kind(const char *fs_type) {
    if (fs_type == NULL) return 0U;
    if (strcmp(fs_type, "fat12") == 0) return 1U;
    if (strcmp(fs_type, "fat32") == 0) return 2U;
    if (strcmp(fs_type, "ext2") == 0) return 3U;
    return 0U;
}

static bool fs_supported_by_drive(const drive_t *drive, uint32_t kind) {
    if (drive == NULL || kind == 0U) return false;
    if (drive->type == DRIVE_TYPE_FDD) return kind == 1U;
    if (drive->type == DRIVE_TYPE_ATA || drive->type == DRIVE_TYPE_AHCI ||
        drive->type == DRIVE_TYPE_PARTITION) return kind == 2U || kind == 3U;
    return false;
}

static bool mount_path_valid(const char *path) {
    size_t length = 0U;
    if (!bounded_text(path, ADMIN_MAINTENANCE_PATH_MAX, &length) ||
        length <= 5U || strncmp(path, "/mnt/", 5U) != 0) return false;
    for (size_t index = 5U; index < length; ++index) {
        char value = path[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' ||
              value == '-')) return false;
    }
    return true;
}

static uint64_t deadline_after(uint64_t now_ms, uint32_t interval_ms) {
    return UINT64_MAX - now_ms < interval_ms
        ? UINT64_MAX : now_ms + interval_ms;
}

static int resource_meta_read(uint32_t resource,
                              admin_resource_meta_t *meta) {
    if (resource >= MAX_DRIVES || meta == NULL) return ADMIN_EINVAL;
    size_t length = 0U;
    return critical_object_read(&protected_resources[resource],
        ADMIN_RESOURCE_VERSION, meta, sizeof(*meta), &length,
        resource_meta_valid) >= 0 && length == sizeof(*meta)
        ? 0 : ADMIN_EINTEGRITY;
}

static int resource_meta_write(uint32_t resource,
                               const admin_resource_meta_t *meta) {
    if (resource >= MAX_DRIVES || meta == NULL) return ADMIN_EINVAL;
    return critical_object_update(&protected_resources[resource],
        ADMIN_RESOURCE_VERSION, meta, sizeof(*meta), resource_meta_valid) == 0
        ? 0 : ADMIN_EINTEGRITY;
}

static uint32_t descendant_mask(uint32_t resource) {
    if (resource >= (uint32_t)drive_count) return 0U;
    uint32_t mask = 1U << resource;
    if (detected_drives[resource].type == DRIVE_TYPE_PARTITION) return mask;
    for (uint32_t child = 0U; child < (uint32_t)drive_count; ++child) {
        if (detected_drives[child].type == DRIVE_TYPE_PARTITION &&
            detected_drives[child].parent_resource == resource)
            mask |= 1U << child;
    }
    return mask;
}

static uint32_t discover_root_resource_mask(void) {
    uint32_t mask = 0U;
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        vfs_mount_info_t info;
        if (vfs_get_mount_info(&detected_drives[resource], &info) != VFS_OK ||
            strcmp(info.path, "/") != 0) continue;
        mask |= 1U << resource;
        if (detected_drives[resource].type == DRIVE_TYPE_PARTITION &&
            detected_drives[resource].parent_resource <
                (uint32_t)drive_count) {
            mask |= 1U << detected_drives[resource].parent_resource;
        }
    }
    return mask;
}

static bool protected_root_resource_mask(uint32_t *mask_out) {
    if (mask_out == NULL) return false;
    admin_control_t control;
    size_t length = 0U;
    if (critical_object_read(&protected_control, ADMIN_CONTROL_VERSION,
            &control, sizeof(control), &length, control_valid) < 0 ||
        length != sizeof(control)) return false;
    *mask_out = control.root_resource_mask;
    return true;
}

bool admin_maintenance_init(void) {
    if (initialized) return true;
    if (!process_cache_rescue_programs()) return false;
    admin_control_t control = {0};
    control.transaction_generation = 1U;
    control.root_resource_mask = discover_root_resource_mask();
    if (control.root_resource_mask == 0U ||
        critical_object_init(&protected_control, ADMIN_CONTROL_VERSION,
            &control, sizeof(control)) != 0) return false;
    memset(saved_mounts, 0, sizeof(saved_mounts));
    memset(active_tokens, 0, sizeof(active_tokens));
    memset(active_fingerprints, 0, sizeof(active_fingerprints));
    for (uint32_t resource = 0U; resource < MAX_DRIVES; ++resource) {
        admin_resource_meta_t meta = {0};
        meta.generation = 1U;
        if (critical_object_init(&protected_resources[resource],
                ADMIN_RESOURCE_VERSION, &meta, sizeof(meta)) != 0)
            return false;
    }
    initialized = true;
    return true;
}

static int validate_target(const admin_storage_request_t *request,
                           uint32_t *resource_mask) {
    if (request == NULL || resource_mask == NULL ||
        request->resource >= (uint32_t)drive_count ||
        request->resource >= MAX_DRIVES) return ADMIN_EINVAL;
    const drive_t *drive = &detected_drives[request->resource];
    if (drive->type != DRIVE_TYPE_ATA && drive->type != DRIVE_TYPE_AHCI &&
        drive->type != DRIVE_TYPE_FDD &&
        drive->type != DRIVE_TYPE_PARTITION) return ADMIN_EINVAL;
    uint32_t mask = request->command == ADMIN_STORAGE_DEVICE_DOWN ||
                    request->command == ADMIN_STORAGE_DEVICE_UP
        ? descendant_mask(request->resource) : 1U << request->resource;
    if (mask == 0U) return ADMIN_EINVAL;
    uint32_t root_mask = 0U;
    if (!protected_root_resource_mask(&root_mask)) return ADMIN_EINTEGRITY;
    if ((mask & root_mask) != 0U) return ADMIN_EROOT;
    if (request->command == ADMIN_STORAGE_MOUNT) {
        uint32_t kind = fs_kind(request->fs_type);
        if (!mount_path_valid(request->mount_path) ||
            !fs_supported_by_drive(drive, kind) || drive->has_partitions)
            return ADMIN_EINVAL;
    } else if (request->fs_type[0] != '\0' ||
               request->mount_path[0] != '\0') {
        return ADMIN_EINVAL;
    }
    *resource_mask = mask;
    return 0;
}

static int claim_transaction(int pid, uint32_t process_generation,
                             const admin_storage_request_t *request,
                             uint32_t resource_mask, uint64_t deadline_ms) {
    uint32_t flags = irq_save();
    admin_control_t control;
    size_t length = 0U;
    bool valid = critical_object_read(&protected_control,
        ADMIN_CONTROL_VERSION, &control, sizeof(control), &length,
        control_valid) >= 0 && length == sizeof(control);
    if (!valid) {
        irq_restore(flags);
        return ADMIN_EINTEGRITY;
    }
    if (control.active != 0U) {
        irq_restore(flags);
        return ADMIN_EBUSY;
    }
    if (control.transaction_generation == UINT32_MAX) {
        irq_restore(flags);
        return ADMIN_EINTEGRITY;
    }
    ++control.transaction_generation;
    control.active = 1U;
    control.owner_pid = pid;
    control.owner_generation = process_generation;
    control.command = request->command;
    control.target = request->resource;
    control.resource_mask = resource_mask;
    control.deadline_ms = deadline_ms;
    valid = critical_object_update(&protected_control,
        ADMIN_CONTROL_VERSION, &control, sizeof(control), control_valid) == 0;
    irq_restore(flags);
    return valid ? 0 : ADMIN_EINTEGRITY;
}

static int finish_transaction(int pid, uint32_t process_generation) {
    uint32_t flags = irq_save();
    admin_control_t control;
    size_t length = 0U;
    bool valid = critical_object_read(&protected_control,
        ADMIN_CONTROL_VERSION, &control, sizeof(control), &length,
        control_valid) >= 0 && length == sizeof(control);
    if (!valid || control.active == 0U || control.owner_pid != pid ||
        control.owner_generation != process_generation) {
        irq_restore(flags);
        return ADMIN_EINTEGRITY;
    }
    control.active = 0U;
    control.owner_pid = 0;
    control.owner_generation = 0U;
    control.command = 0U;
    control.target = 0U;
    control.resource_mask = 0U;
    control.deadline_ms = 0U;
    valid = critical_object_update(&protected_control,
        ADMIN_CONTROL_VERSION, &control, sizeof(control), control_valid) == 0;
    irq_restore(flags);
    return valid ? 0 : ADMIN_EINTEGRITY;
}

static int save_mount_snapshot(uint32_t resource) {
    if (resource >= (uint32_t)drive_count) return ADMIN_EINVAL;
    admin_resource_meta_t meta;
    if (resource_meta_read(resource, &meta) != 0) return ADMIN_EINTEGRITY;
    vfs_mount_info_t info;
    if (vfs_get_mount_info(&detected_drives[resource], &info) != VFS_OK) {
        memset(&saved_mounts[resource], 0, sizeof(saved_mounts[resource]));
        meta.saved_mount = 0U;
        meta.saved_fs_kind = 0U;
        meta.saved_crc32 = 0U;
        return resource_meta_write(resource, &meta);
    }
    size_t path_length = 0U, fs_length = 0U;
    uint32_t kind = fs_kind(info.fs_type);
    if (!bounded_text(info.path, sizeof(info.path), &path_length) ||
        !bounded_text(info.fs_type, sizeof(info.fs_type), &fs_length) ||
        path_length >= sizeof(saved_mounts[resource].path) ||
        fs_length >= sizeof(saved_mounts[resource].fs_type) || kind == 0U ||
        strcmp(info.path, "/") == 0) return ADMIN_EINTEGRITY;
    memset(&saved_mounts[resource], 0, sizeof(saved_mounts[resource]));
    strcpy(saved_mounts[resource].path, info.path);
    strcpy(saved_mounts[resource].fs_type, info.fs_type);
    meta.saved_mount = 1U;
    meta.saved_fs_kind = kind;
    meta.saved_crc32 = text_crc32(saved_mounts[resource].path,
        sizeof(saved_mounts[resource].path),
        saved_mounts[resource].fs_type,
        sizeof(saved_mounts[resource].fs_type));
    return resource_meta_write(resource, &meta);
}

static int save_requested_mount(uint32_t resource, const char *path,
                                const char *fs_type) {
    if (resource >= (uint32_t)drive_count || !mount_path_valid(path) ||
        !fs_supported_by_drive(&detected_drives[resource], fs_kind(fs_type)))
        return ADMIN_EINVAL;
    admin_resource_meta_t meta;
    if (resource_meta_read(resource, &meta) != 0) return ADMIN_EINTEGRITY;
    memset(&saved_mounts[resource], 0, sizeof(saved_mounts[resource]));
    strcpy(saved_mounts[resource].path, path);
    strcpy(saved_mounts[resource].fs_type, fs_type);
    meta.saved_mount = 1U;
    meta.saved_fs_kind = fs_kind(fs_type);
    meta.saved_crc32 = text_crc32(saved_mounts[resource].path,
        sizeof(saved_mounts[resource].path),
        saved_mounts[resource].fs_type,
        sizeof(saved_mounts[resource].fs_type));
    return resource_meta_write(resource, &meta);
}

static bool saved_mount_valid(uint32_t resource,
                              admin_resource_meta_t *meta_out) {
    admin_resource_meta_t meta;
    if (resource_meta_read(resource, &meta) != 0 || meta.saved_mount == 0U ||
        !mount_path_valid(saved_mounts[resource].path) ||
        fs_kind(saved_mounts[resource].fs_type) != meta.saved_fs_kind ||
        !fs_supported_by_drive(&detected_drives[resource],
                               meta.saved_fs_kind) ||
        text_crc32(saved_mounts[resource].path,
            sizeof(saved_mounts[resource].path),
            saved_mounts[resource].fs_type,
            sizeof(saved_mounts[resource].fs_type)) != meta.saved_crc32)
        return false;
    if (meta_out != NULL) *meta_out = meta;
    return true;
}

static int acquire_leases(int pid, uint32_t process_generation,
                          uint32_t resource_mask, uint64_t now_ms) {
    memset(active_tokens, 0, sizeof(active_tokens));
    memset(active_fingerprints, 0, sizeof(active_fingerprints));
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        if (!storage_service_expected_fingerprint(
                resource, &active_fingerprints[resource])) return ADMIN_EIO;
        int result = storage_maintenance_acquire(pid, process_generation,
            resource, active_fingerprints[resource], now_ms,
            &active_tokens[resource]);
        if (result != 0) return result;
    }
    return 0;
}

static bool leases_valid(int pid, uint32_t process_generation,
                         uint32_t resource_mask) {
    uint64_t now_ms = pit_monotonic_ms();
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        if (!storage_maintenance_valid(pid, process_generation,
                active_tokens[resource], active_fingerprints[resource],
                now_ms)) return false;
    }
    return true;
}

static int release_leases(int pid, uint32_t process_generation,
                          uint32_t resource_mask) {
    int result = 0;
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U ||
            active_tokens[resource] == STORAGE_MAINTENANCE_INVALID_TOKEN)
            continue;
        if (storage_maintenance_release(pid, process_generation,
                active_tokens[resource]) != 0) result = ADMIN_EINTEGRITY;
        active_tokens[resource] = STORAGE_MAINTENANCE_INVALID_TOKEN;
        active_fingerprints[resource] = 0U;
    }
    return result;
}

static int block_and_drain(uint32_t resource_mask, uint64_t deadline_ms) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        vfs_mount_info_t info;
        if (vfs_get_mount_info(&detected_drives[resource], &info) != VFS_OK)
            continue;
        if (vfs_maintenance_begin(&detected_drives[resource]) != VFS_OK)
            return ADMIN_EIO;
    }
    for (;;) {
        uint32_t open_nodes = 0U;
        for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
             ++resource) {
            if ((resource_mask & (1U << resource)) == 0U) continue;
            uint32_t count = 0U;
            int status = vfs_maintenance_open_count(
                &detected_drives[resource], &count);
            if (status == VFS_ERR_NOT_FOUND) continue;
            if (status != VFS_OK || UINT32_MAX - open_nodes < count)
                return ADMIN_EINTEGRITY;
            open_nodes += count;
        }
        if (open_nodes == 0U) return 0;
        if (pit_monotonic_ms() >= deadline_ms) break;
        if (scheduler_sleep_ms(ADMIN_DRAIN_POLL_MS) != 0) return ADMIN_EIO;
    }
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        uint32_t revoked = 0U;
        if (process_revoke_files_for_resource(resource, &revoked) != 0)
            return ADMIN_EIO;
        uint32_t remaining = 0U;
        int status = vfs_maintenance_open_count(
            &detected_drives[resource], &remaining);
        if (status != VFS_ERR_NOT_FOUND &&
            (status != VFS_OK || remaining != 0U)) return ADMIN_ETIMEDOUT;
    }
    return 0;
}

static int flush_resources(uint32_t resource_mask) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        drive_t *drive = &detected_drives[resource];
        if (drive->type == DRIVE_TYPE_PARTITION &&
            drive->parent_resource < (uint32_t)drive_count &&
            (resource_mask & (1U << drive->parent_resource)) != 0U)
            continue;
        if (block_device_flush(drive) != BLOCK_DEVICE_OK) return ADMIN_EIO;
    }
    return 0;
}

static int unmount_resources(uint32_t resource_mask) {
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
             ++resource) {
            if ((resource_mask & (1U << resource)) == 0U) continue;
            bool child = detected_drives[resource].type ==
                         DRIVE_TYPE_PARTITION;
            if ((pass == 0U) != child) continue;
            vfs_mount_info_t info;
            if (vfs_get_mount_info(&detected_drives[resource], &info) !=
                    VFS_OK) continue;
            if (vfs_unmount(info.path) != VFS_OK) return ADMIN_EIO;
        }
    }
    return 0;
}

static int requalify_resources(uint32_t resource_mask) {
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
             ++resource) {
            if ((resource_mask & (1U << resource)) == 0U) continue;
            bool child = detected_drives[resource].type ==
                         DRIVE_TYPE_PARTITION;
            if ((pass == 0U) == child) continue;
            uint32_t fingerprint = 0U;
            if (!storage_service_requalify_media(resource, &fingerprint) ||
                fingerprint != active_fingerprints[resource])
                return ADMIN_EIO;
        }
    }
    return 0;
}

static int restore_mounts(uint32_t resource_mask) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        admin_resource_meta_t meta;
        if (!saved_mount_valid(resource, &meta)) {
            if (resource_meta_read(resource, &meta) != 0)
                return ADMIN_EINTEGRITY;
            if (meta.saved_mount == 0U) continue;
            return ADMIN_EINTEGRITY;
        }
        if (vfs_mount_maintenance(&detected_drives[resource],
                saved_mounts[resource].fs_type,
                saved_mounts[resource].path) != VFS_OK) return ADMIN_EIO;
    }
    return 0;
}

static void release_vfs_blocks(uint32_t resource_mask) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        vfs_mount_info_t info;
        if (vfs_get_mount_info(&detected_drives[resource], &info) == VFS_OK)
            (void)vfs_maintenance_release(&detected_drives[resource]);
    }
}

static void block_vfs_mounts(uint32_t resource_mask) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        vfs_mount_info_t info;
        if (vfs_get_mount_info(&detected_drives[resource], &info) == VFS_OK)
            (void)vfs_maintenance_begin(&detected_drives[resource]);
    }
}

static int increment_generations(uint32_t resource_mask) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) == 0U) continue;
        admin_resource_meta_t meta;
        if (resource_meta_read(resource, &meta) != 0 ||
            meta.generation == UINT32_MAX) return ADMIN_EINTEGRITY;
        ++meta.generation;
        if (resource_meta_write(resource, &meta) != 0)
            return ADMIN_EINTEGRITY;
    }
    return 0;
}

static int populate_result(uint32_t resource, admin_storage_result_t *result) {
    if (result == NULL || resource >= (uint32_t)drive_count)
        return ADMIN_EINVAL;
    admin_resource_meta_t meta;
    if (resource_meta_read(resource, &meta) != 0) return ADMIN_EINTEGRITY;
    memset(result, 0, sizeof(*result));
    result->version = ADMIN_MAINTENANCE_ABI_VERSION;
    result->struct_size = sizeof(*result);
    result->resource = resource;
    result->generation = meta.generation;
    result->drive_type = (uint32_t)detected_drives[resource].type;
    result->parent_resource = UINT32_MAX;
    if (detected_drives[resource].type == DRIVE_TYPE_PARTITION)
        result->parent_resource = detected_drives[resource].parent_resource;
    strncpy(result->name, detected_drives[resource].name,
            sizeof(result->name) - 1U);
    vfs_mount_info_t info;
    if (vfs_get_mount_info(&detected_drives[resource], &info) == VFS_OK) {
        result->flags |= ADMIN_RESOURCE_MOUNTED;
        if (info.maintenance_blocked) result->flags |= ADMIN_RESOURCE_BLOCKED;
        result->open_handles = info.open_nodes;
        strncpy(result->fs_type, info.fs_type, sizeof(result->fs_type) - 1U);
        strncpy(result->mount_path, info.path,
                sizeof(result->mount_path) - 1U);
    }
    uint32_t root_mask = 0U;
    if (!protected_root_resource_mask(&root_mask)) return ADMIN_EINTEGRITY;
    if ((root_mask & (1U << resource)) != 0U)
        result->flags |= ADMIN_RESOURCE_ROOT;
    if (detected_drives[resource].has_partitions)
        result->flags |= ADMIN_RESOURCE_PARENT;
    if (storage_service_resource_available(resource))
        result->flags |= ADMIN_RESOURCE_AVAILABLE;
    if (storage_service_resource_read_only(resource))
        result->flags |= ADMIN_RESOURCE_READ_ONLY;
    if (storage_service_resource_admin_failed(resource))
        result->state = ADMIN_RESOURCE_FAILED;
    else if (storage_service_resource_admin_transition(resource))
        result->state = ADMIN_RESOURCE_TRANSITION;
    else if (storage_service_resource_admin_down(resource))
        result->state = ADMIN_RESOURCE_DOWN;
    else
        result->state = ADMIN_RESOURCE_ONLINE;
    return 0;
}

static int execute_down(int pid, uint32_t process_generation,
                        uint32_t resource_mask, uint64_t deadline_ms) {
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
         ++resource) {
        if ((resource_mask & (1U << resource)) != 0U &&
            save_mount_snapshot(resource) != 0) return ADMIN_EINTEGRITY;
    }
    int result = block_and_drain(resource_mask, deadline_ms);
    if (result != 0 || !leases_valid(pid, process_generation, resource_mask))
        return result != 0 ? result : ADMIN_EACCES;
    if (!storage_service_admin_begin(resource_mask, false))
        return ADMIN_ESTATE;
    if (flush_resources(resource_mask) != 0 ||
        !leases_valid(pid, process_generation, resource_mask) ||
        unmount_resources(resource_mask) != 0 ||
        !leases_valid(pid, process_generation, resource_mask) ||
        !storage_service_admin_finish_down(resource_mask)) {
        (void)storage_service_admin_fail(resource_mask);
        return ADMIN_EIO;
    }
    return increment_generations(resource_mask);
}

static int execute_up(int pid, uint32_t process_generation,
                      uint32_t resource_mask, uint64_t deadline_ms) {
    int result = block_and_drain(resource_mask, deadline_ms);
    if (result != 0 || !leases_valid(pid, process_generation, resource_mask))
        return result != 0 ? result : ADMIN_EACCES;
    if (!storage_service_admin_begin(resource_mask, true))
        return ADMIN_ESTATE;
    if (flush_resources(resource_mask) != 0 ||
        unmount_resources(resource_mask) != 0 ||
        requalify_resources(resource_mask) != 0 ||
        restore_mounts(resource_mask) != 0 ||
        !leases_valid(pid, process_generation, resource_mask) ||
        !storage_service_admin_finish_up(resource_mask)) {
        (void)storage_service_admin_fail(resource_mask);
        return ADMIN_EIO;
    }
    release_vfs_blocks(resource_mask);
    return increment_generations(resource_mask);
}

static int execute_umount(int pid, uint32_t process_generation,
                          uint32_t resource_mask, uint64_t deadline_ms) {
    uint32_t resource = 0U;
    while ((resource_mask & (1U << resource)) == 0U) ++resource;
    if (save_mount_snapshot(resource) != 0) return ADMIN_EINTEGRITY;
    int result = block_and_drain(resource_mask, deadline_ms);
    if (result != 0 || !leases_valid(pid, process_generation, resource_mask))
        return result != 0 ? result : ADMIN_EACCES;
    if (!storage_service_admin_begin(resource_mask, false))
        return ADMIN_ESTATE;
    if (flush_resources(resource_mask) != 0 ||
        !leases_valid(pid, process_generation, resource_mask) ||
        unmount_resources(resource_mask) != 0 ||
        !leases_valid(pid, process_generation, resource_mask) ||
        !storage_service_admin_finish_online(resource_mask)) {
        (void)storage_service_admin_fail(resource_mask);
        return ADMIN_EIO;
    }
    return increment_generations(resource_mask);
}

static int execute_mount(int pid, uint32_t process_generation,
                         const admin_storage_request_t *request,
                         uint32_t resource_mask) {
    uint32_t resource = request->resource;
    vfs_mount_info_t existing;
    if (vfs_get_mount_info(&detected_drives[resource], &existing) == VFS_OK)
        return strcmp(existing.path, request->mount_path) == 0 &&
               strcmp(existing.fs_type, request->fs_type) == 0
            ? 0 : ADMIN_EBUSY;
    if (!leases_valid(pid, process_generation, resource_mask) ||
        !storage_service_admin_begin(resource_mask, false))
        return ADMIN_ESTATE;
    uint32_t fingerprint = 0U;
    if (!storage_service_requalify_media(resource, &fingerprint) ||
        fingerprint != active_fingerprints[resource] ||
        !leases_valid(pid, process_generation, resource_mask) ||
        vfs_mount_maintenance(&detected_drives[resource], request->fs_type,
                              request->mount_path) != VFS_OK ||
        save_requested_mount(resource, request->mount_path,
                             request->fs_type) != 0 ||
        !leases_valid(pid, process_generation, resource_mask) ||
        !storage_service_admin_finish_online(resource_mask)) {
        (void)storage_service_admin_fail(resource_mask);
        return ADMIN_EIO;
    }
    release_vfs_blocks(resource_mask);
    return increment_generations(resource_mask);
}

int admin_maintenance_execute(int pid, uint32_t process_generation,
                              const admin_storage_request_t *request,
                              admin_storage_result_t *result,
                              uint64_t now_ms) {
    if (!initialized || pid <= 0 || process_generation == 0U ||
        request == NULL || result == NULL ||
        request->version != ADMIN_MAINTENANCE_ABI_VERSION ||
        request->struct_size != sizeof(*request) || request->reserved != 0U ||
        request->command > ADMIN_STORAGE_UMOUNT ||
        !bounded_text(request->fs_type, sizeof(request->fs_type), NULL) ||
        !bounded_text(request->mount_path, sizeof(request->mount_path), NULL) ||
        !process_identity_alive(pid, process_generation)) return ADMIN_EINVAL;
    if (request->command == ADMIN_STORAGE_STATUS)
        return populate_result(request->resource, result);

    uint32_t resource_mask = 0U;
    int validation = validate_target(request, &resource_mask);
    if (validation != 0) return validation;

    bool already_down = storage_service_resource_admin_down(
        request->resource);
    if (request->command == ADMIN_STORAGE_DEVICE_DOWN && already_down)
        return populate_result(request->resource, result);
    if (request->command == ADMIN_STORAGE_DEVICE_UP && !already_down)
        return populate_result(request->resource, result);
    if (request->command == ADMIN_STORAGE_UMOUNT) {
        vfs_mount_info_t info;
        if (vfs_get_mount_info(&detected_drives[request->resource], &info) !=
                VFS_OK) return populate_result(request->resource, result);
    }

    uint32_t drain_ms = request->drain_timeout_ms == 0U
        ? ADMIN_MAINTENANCE_DRAIN_DEFAULT_MS : request->drain_timeout_ms;
    if (drain_ms > ADMIN_MAINTENANCE_DRAIN_MAX_MS) return ADMIN_EINVAL;
    uint64_t deadline_ms = deadline_after(now_ms, drain_ms);
    int status = claim_transaction(pid, process_generation, request,
                                   resource_mask, deadline_ms);
    if (status != 0) return status;
    status = acquire_leases(pid, process_generation, resource_mask, now_ms);
    bool executed = false;
    if (status == 0) {
        executed = true;
        if (request->command == ADMIN_STORAGE_DEVICE_DOWN)
            status = execute_down(pid, process_generation, resource_mask,
                                  deadline_ms);
        else if (request->command == ADMIN_STORAGE_DEVICE_UP)
            status = execute_up(pid, process_generation, resource_mask,
                                deadline_ms);
        else if (request->command == ADMIN_STORAGE_MOUNT)
            status = execute_mount(pid, process_generation, request,
                                   resource_mask);
        else
            status = execute_umount(pid, process_generation, resource_mask,
                                    deadline_ms);
    }
    int release_status = release_leases(pid, process_generation,
                                        resource_mask);
    int finish_status = finish_transaction(pid, process_generation);
    if (status == 0 && (release_status != 0 || finish_status != 0)) {
        block_vfs_mounts(resource_mask);
        (void)storage_service_admin_fail(resource_mask);
        status = ADMIN_EINTEGRITY;
    }
    if (status != 0 && executed) {
        block_vfs_mounts(resource_mask);
        (void)storage_service_admin_fail(resource_mask);
    }
    if (status != 0) return status;
    return populate_result(request->resource, result);
}

void admin_maintenance_process_cleanup(int pid,
                                       uint32_t process_generation) {
    if (!initialized || pid <= 0 || process_generation == 0U) return;
    admin_control_t control;
    size_t length = 0U;
    uint32_t flags = irq_save();
    bool owned = critical_object_read(&protected_control,
        ADMIN_CONTROL_VERSION, &control, sizeof(control), &length,
        control_valid) >= 0 && length == sizeof(control) &&
        control.active != 0U && control.owner_pid == pid &&
        control.owner_generation == process_generation;
    irq_restore(flags);
    uint32_t keep_blocked = owned ? control.resource_mask : 0U;
    if (owned) {
        for (uint32_t resource = 0U; resource < (uint32_t)drive_count;
             ++resource) {
            if ((keep_blocked & (1U << resource)) == 0U) continue;
            uint32_t revoked = 0U;
            (void)process_revoke_files_for_resource(resource, &revoked);
        }
        (void)storage_service_admin_fail(keep_blocked);
        (void)finish_transaction(pid, process_generation);
    }
    uint32_t released = storage_maintenance_process_cleanup(
        pid, process_generation);
    release_vfs_blocks(released & ~keep_blocked);
}
