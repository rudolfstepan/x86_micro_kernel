/**
 * @file fs/vfs/vfs.c
 * @brief VFS-Dispatch, Mounttabelle und Pfadauflösung.
 *
 * Layer: Ring-0 virtual filesystem and on-disk format.
 * Contract: On-Disk-Werte, Pfade und Ressourcenbereiche werden vor I/O validiert.
 * Safety: Backends werden erst nach vollständigem Mount publiziert; Unmount widerruft Zugriffe.
 */
#include "vfs.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#if !defined(KERNEL_HOST_TEST) || defined(FILE_OBJECT_GUARD_VFS_TEST)
#define VFS_FILE_OBJECT_GUARD 1
#include "include/kernel/file_object_guard.h"
#ifndef KERNEL_HOST_TEST
#include "include/kernel/storage_service.h"
#include "kernel/proc/process.h"
#endif
#endif
#ifndef KERNEL_HOST_TEST
#include "include/kernel/filesystem_safety.h"
#include "include/kernel/panic.h"
#include "kernel/sched/mutex.h"
#include "kernel/sched/scheduler.h"
#include "kernel/time/pit.h"
#endif

// ===========================================================================
// VFS Internal State
// ===========================================================================

#define MAX_FILESYSTEMS 10
#define MAX_MOUNTS 10

typedef struct {
    char name[32];
    vfs_filesystem_ops_t* ops;
    bool registered;
} fs_registration_t;

static fs_registration_t registered_filesystems[MAX_FILESYSTEMS];
static vfs_mount_t* mount_list = NULL;
static int fs_count = 0;
static int mount_count = 0;

typedef struct {
    vfs_filesystem_t* fs;
    vfs_node_t* node;
} vfs_open_node_record_t;

static vfs_open_node_record_t open_node_records[VFS_OPEN_NODE_CAPACITY];

#ifdef VFS_FILE_OBJECT_GUARD
static file_object_guard_t vfs_object_guards;
static int vfs_guard_mutation_error;
static bool vfs_guard_initialized;
static volatile uint32_t vfs_guard_pending_media;
#ifdef KERNEL_HOST_TEST
extern drive_t* vfs_guard_platform_drive(uint32_t resource);
extern uint64_t vfs_guard_platform_now(void);
extern bool vfs_guard_platform_live(int pid, uint32_t generation);
extern bool vfs_guard_platform_available(uint32_t resource);
extern bool vfs_guard_platform_fence(uint32_t resource);
#else
static drive_t* vfs_guard_platform_drive(uint32_t resource) {
    return drive_count > 0 && resource < (uint32_t)drive_count &&
        resource < FILE_OBJECT_GUARD_RESOURCES ? &detected_drives[resource] : NULL;
}
static uint64_t vfs_guard_platform_now(void) { return pit_monotonic_ms(); }
static bool vfs_guard_platform_live(int pid, uint32_t generation) {
    return process_file_object_owner_live(pid, generation);
}
static bool vfs_guard_platform_available(uint32_t resource) {
    return storage_service_resource_available(resource);
}
static bool vfs_guard_platform_fence(uint32_t resource) {
    return storage_service_report_media_failure(resource, true);
}
#endif

static int vfs_guard_errno(int result) {
    return result == 0 ? VFS_OK : result == -REIST_EBUSY || result == -REIST_EAGAIN
        ? VFS_ERR_BUSY : VFS_ERR_IO;
}

static int vfs_guard_drive_index(const drive_t* drive) {
    for (uint32_t i = 0; i < FILE_OBJECT_GUARD_RESOURCES; ++i)
        if (vfs_guard_platform_drive(i) == drive) return (int)i;
    return -1;
}

static uint64_t vfs_guard_drive_size(const drive_t* drive) {
    if (!drive) return 0;
    if (drive->sectors) return drive->sectors;
    if (drive->type == DRIVE_TYPE_FDD) {
        uint64_t tracks = (uint64_t)drive->cylinder * drive->head;
        if (drive->sector && tracks > UINT64_MAX / drive->sector) return 0;
        return tracks * drive->sector;
    }
    return 0;
}

static const drive_t* vfs_guard_physical(const drive_t* drive) {
    if (!drive) return NULL;
    if (drive->type != DRIVE_TYPE_PARTITION)
        return drive->type == DRIVE_TYPE_ATA || drive->type == DRIVE_TYPE_AHCI ||
            drive->type == DRIVE_TYPE_FDD ? drive : NULL;
    const drive_t* parent = vfs_guard_platform_drive(drive->parent_resource);
    if (!parent || parent->type == DRIVE_TYPE_PARTITION || !drive->sectors ||
        (uint64_t)drive->lba_offset + drive->sectors > vfs_guard_drive_size(parent))
        return NULL;
    return vfs_guard_physical(parent);
}

static bool vfs_guard_same_physical(const drive_t* a, const drive_t* b) {
    a = vfs_guard_physical(a); b = vfs_guard_physical(b);
    if (!a || !b || a->type != b->type) return false;
    if (a->type == DRIVE_TYPE_ATA) return a->base == b->base && a->is_master == b->is_master;
    if (a->type == DRIVE_TYPE_AHCI)
        return a->ahci_controller == b->ahci_controller && a->ahci_port == b->ahci_port;
    if (a->type == DRIVE_TYPE_FDD) return a->fdd_drive_no == b->fdd_drive_no;
    return false;
}

static int vfs_guard_extent(const vfs_filesystem_t* fs, uint32_t* first,
                             uint32_t* count) {
    if (!fs || !fs->drive || !fs->ops || !fs->ops->volume_extent ||
        fs->ops->volume_extent(fs, first, count) != VFS_OK || !*count)
        return -REIST_ENOTSUP;
    const drive_t* physical = vfs_guard_physical(fs->drive);
    uint64_t logical_end = (uint64_t)*first + *count;
    if (!physical || logical_end > vfs_guard_drive_size(fs->drive)) return -REIST_EINVAL;
    uint64_t absolute_first = *first;
    if (fs->drive->type == DRIVE_TYPE_PARTITION) absolute_first += fs->drive->lba_offset;
    if (absolute_first > UINT32_MAX) return -REIST_EINVAL;
    *first = (uint32_t)absolute_first;
    uint64_t end = absolute_first + *count;
    if (!physical || end > vfs_guard_drive_size(physical) ||
        (fs->drive->type == DRIVE_TYPE_PARTITION &&
         (*first < fs->drive->lba_offset ||
          end > (uint64_t)fs->drive->lba_offset + fs->drive->sectors))) return -REIST_EINVAL;
    return 0;
}

static int vfs_guard_resource(const vfs_filesystem_t* fs, uint32_t* resource) {
    uint32_t first, count;
    int result = vfs_guard_extent(fs, &first, &count);
    if (result) return result;
    int canonical = vfs_guard_drive_index(fs->drive);
    if (canonical < 0) return -REIST_ENODEV;
    for (vfs_mount_t* m = mount_list; m; m = m->next) {
        if (m->fs == fs || !vfs_guard_same_physical(fs->drive, m->fs->drive)) continue;
        uint32_t other_first, other_count;
        result = vfs_guard_extent(m->fs, &other_first, &other_count);
        if (result) return result;
        if ((uint64_t)first >= (uint64_t)other_first + other_count ||
            (uint64_t)other_first >= (uint64_t)first + count) continue;
        if (first != other_first || count != other_count || strcmp(fs->name, m->fs->name))
            return -REIST_EINVAL;
        int index = vfs_guard_drive_index(m->fs->drive);
        if (index < 0) return -REIST_ENODEV;
        if (index < canonical) canonical = index;
    }
    *resource = (uint32_t)canonical;
    return 0;
}

static bool vfs_guard_kind(const vfs_filesystem_t* fs, uint32_t kind) {
    return (kind == REIST_FILE_OBJECT_FAT12 && !strcmp(fs->name, "fat12")) ||
        (kind == REIST_FILE_OBJECT_FAT32 && !strcmp(fs->name, "fat32")) ||
        (kind == REIST_FILE_OBJECT_EXT2 && !strcmp(fs->name, "ext2"));
}

static int vfs_guard_normalize(reist_file_object_key_t* key) {
    drive_t* requested = vfs_guard_platform_drive(key->resource);
    if (!requested || !vfs_guard_physical(requested)) return -REIST_ENODEV;
    vfs_filesystem_t* selected = NULL;
    for (vfs_mount_t* m = mount_list; m; m = m->next) {
        if (m->fs->drive == requested) { selected = m->fs; break; }
    }
    if (!selected) {
        uint32_t requested_first = requested->type == DRIVE_TYPE_PARTITION ? requested->lba_offset : 0;
        uint64_t requested_count = vfs_guard_drive_size(requested);
        for (vfs_mount_t* m = mount_list; m; m = m->next) {
            if (!vfs_guard_same_physical(requested, m->fs->drive)) continue;
            uint32_t first, count;
            if (vfs_guard_extent(m->fs, &first, &count) ||
                first != requested_first || count != requested_count) continue;
            if (selected && selected != m->fs) return -REIST_EINVAL;
            selected = m->fs;
        }
    }
    if (!selected || !vfs_guard_kind(selected, key->kind)) return -REIST_ENOTSUP;
    if (selected->object_media_revoked) return -REIST_EIO;
    if (selected->maintenance_blocked) return -REIST_EBUSY;
    return vfs_guard_resource(selected, &key->resource);
}

static int vfs_guard_node_key(const vfs_node_t* node, reist_file_object_key_t* key) {
    if (!node || !node->fs || !node->fs->ops || !node->fs->ops->object_key ||
        node->fs->ops->object_key(node, key) != VFS_OK) return -REIST_ENOTSUP;
    int result = vfs_guard_resource(node->fs, &key->resource);
    if (!result && !file_object_guard_key_valid(key)) result = -REIST_EIO;
    return result;
}

/* Reject an overlapping second mount before its backend can recover media. */
static int vfs_guard_mount_admission(const drive_t* drive) {
    const drive_t* physical = vfs_guard_physical(drive);
    uint64_t count = vfs_guard_drive_size(drive);
    uint32_t first = drive->type == DRIVE_TYPE_PARTITION ? drive->lba_offset : 0;
    if (!physical || !count || count > UINT32_MAX ||
        (uint64_t)first + count > vfs_guard_drive_size(physical)) return VFS_ERR_INVALID;
    for (vfs_mount_t* m = mount_list; m; m = m->next) {
        if (!vfs_guard_same_physical(drive, m->fs->drive)) continue;
        uint32_t other_first, other_count;
        if (vfs_guard_extent(m->fs, &other_first, &other_count)) return VFS_ERR_BUSY;
        if ((uint64_t)first < (uint64_t)other_first + other_count &&
            (uint64_t)other_first < (uint64_t)first + count) return VFS_ERR_BUSY;
    }
    return VFS_OK;
}
#endif

#ifdef VFS_FILE_OBJECT_GUARD
static int vfs_guard_open_check(vfs_filesystem_t* fs) {
    if (fs->object_media_revoked) return VFS_ERR_IO;
    uint32_t resource;
    int result = vfs_guard_resource(fs, &resource);
    if (!result) result = file_object_guard_can_open(&vfs_object_guards, resource,
                                                     vfs_guard_platform_now());
    return vfs_guard_errno(result);
}

static int vfs_guard_pin_count(vfs_filesystem_t* fs, uint32_t* count) {
    uint32_t resource;
    int result = vfs_guard_resource(fs, &resource);
    if (!result) result = file_object_guard_count(&vfs_object_guards, resource,
                                                  count, vfs_guard_platform_now());
    return vfs_guard_errno(result);
}

static void vfs_guard_publish_fences(void) {
    if (!vfs_guard_initialized) return;
    static uint32_t reported;
    uint32_t mask = 0;
    int result = file_object_guard_fenced(&vfs_object_guards, &mask);
    if (result == -REIST_EBUSY) return;
    for (uint32_t i = 0; i < FILE_OBJECT_GUARD_RESOURCES; ++i) {
        uint32_t bit = 1U << i;
        if ((mask & bit) && !(reported & bit) && vfs_guard_platform_drive(i) &&
            vfs_guard_platform_fence(i)) reported |= bit;
    }
}

static int vfs_guard_apply_media_changes(void) {
    if (!vfs_guard_initialized) return 0;
    uint32_t pending = __atomic_exchange_n(&vfs_guard_pending_media, 0U, __ATOMIC_ACQ_REL);
    for (uint32_t i = 0; i < FILE_OBJECT_GUARD_RESOURCES; ++i) {
        uint32_t bit = 1U << i;
        if (!(pending & bit)) continue;
        int result = file_object_guard_revoke_media(&vfs_object_guards, i);
        if (result) {
            __atomic_fetch_or(&vfs_guard_pending_media, pending, __ATOMIC_RELEASE);
            return result;
        }
        /* A republished device is not the mount that produced existing legacy
         * nodes (including its shared root). Never refresh their binding in
         * place. Close then explicit unmount/remount establishes a new one. */
        for (vfs_mount_t* m = mount_list; m; m = m->next)
            if (vfs_guard_drive_index(m->fs->drive) == (int)i)
                m->fs->object_media_revoked = true;
        pending &= ~bit;
    }
    return 0;
}
#endif

static int vfs_open_record_free_slot(void) {
    for (uint32_t index = 0U; index < VFS_OPEN_NODE_CAPACITY; ++index)
        if (open_node_records[index].node == NULL) return (int)index;
    return -1;
}

static int vfs_open_record_find(vfs_node_t* node) {
    for (uint32_t index = 0U; index < VFS_OPEN_NODE_CAPACITY; ++index)
        if (open_node_records[index].node == node) return (int)index;
    return -1;
}

static bool vfs_filesystem_has_registered_nodes(vfs_filesystem_t* fs) {
    for (uint32_t index = 0U; index < VFS_OPEN_NODE_CAPACITY; ++index)
        if (open_node_records[index].fs == fs &&
            open_node_records[index].node != NULL) return true;
    return false;
}

/* A recursive mutex preserves existing callback re-entry while allowing a
 * contending foreground task to sleep instead of blocking another CPU. */
#define VFS_OPERATION_LOCK_TIMEOUT_MS 10000U
#ifndef KERNEL_HOST_TEST
static kernel_mutex_t vfs_operation_mutex = KERNEL_MUTEX_INIT;
#endif

static void vfs_operation_end(void);

static bool vfs_operation_begin(void) {
#ifndef KERNEL_HOST_TEST
    KASSERT_NOT_IRQ();
    if (!scheduler_can_sleep()) {
        printf("VFS_SLEEP_CONTEXT irq_enabled=%u irq_context=%u preempt=%u\n",
               irq_enabled() ? 1U : 0U, irq_in_context() ? 1U : 0U,
               scheduler_preempt_is_disabled() ? 1U : 0U);
    }
    if (kernel_mutex_lock_for(&vfs_operation_mutex,
                              VFS_OPERATION_LOCK_TIMEOUT_MS) != 0) return false;
#endif
#ifdef VFS_FILE_OBJECT_GUARD
    if (vfs_guard_apply_media_changes()) { vfs_operation_end(); return false; }
#endif
    return true;
}

static void vfs_operation_end(void) {
#ifdef VFS_FILE_OBJECT_GUARD
    vfs_guard_publish_fences();
#endif
#ifndef KERNEL_HOST_TEST
    KASSERT_NOT_IRQ();
    kernel_mutex_unlock(&vfs_operation_mutex);
#endif
}

#ifdef VFS_FILE_OBJECT_GUARD
static bool vfs_guard_try_begin(void) {
#ifndef KERNEL_HOST_TEST
    if (kernel_mutex_lock_for(&vfs_operation_mutex, 0) != 0) return false;
#endif
    if (vfs_guard_apply_media_changes()) { vfs_operation_end(); return false; }
    return true;
}
#endif

void vfs_file_object_guard_media_changed(uint32_t resource) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (resource >= FILE_OBJECT_GUARD_RESOURCES) return;
    uint32_t mask = 1U << resource;
    const drive_t* changed = vfs_guard_platform_drive(resource);
    for (uint32_t i = 0; changed && i < FILE_OBJECT_GUARD_RESOURCES; ++i)
        if (vfs_guard_same_physical(changed, vfs_guard_platform_drive(i))) mask |= 1U << i;
    __atomic_fetch_or(&vfs_guard_pending_media, mask, __ATOMIC_RELEASE);
#else
    (void)resource;
#endif
}

void vfs_file_object_guard_process_cleanup(int pid, uint32_t generation) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (!vfs_guard_initialized || !vfs_guard_try_begin()) return;
    (void)file_object_guard_cleanup(&vfs_object_guards, (file_object_owner_t){pid, generation});
    vfs_operation_end();
    /* A contended cleanup retains its deny-only entries. The bounded poll
     * sweep retries exact dead generations, never regranting their tokens. */
#else
    (void)pid; (void)generation;
#endif
}

int vfs_file_object_guard_poll(uint64_t now_ms) {
#ifdef VFS_FILE_OBJECT_GUARD
    static uint32_t cursor;
    if (!vfs_guard_initialized) return 0;
    if (!vfs_guard_try_begin()) return -REIST_EBUSY;
    int result = file_object_guard_poll(&vfs_object_guards, now_ms);
    /* Check the mutation owner EVERY poll, then one of the sixteen pin slots.
     * Thus an old service reservation is fenced before service restart. */
    for (unsigned step = 0; !result && step < 2; ++step) {
        uint32_t slot = step == 0 ? FILE_OBJECT_GUARD_CAPACITY : cursor;
        file_object_owner_t service = {0}, client = {0};
        result = file_object_guard_owner_at(&vfs_object_guards, slot, &service, &client);
        if (!result && service.pid && !vfs_guard_platform_live(service.pid, service.generation))
            result = file_object_guard_cleanup(&vfs_object_guards, service);
        if (!result && client.pid && !vfs_guard_platform_live(client.pid, client.generation))
            result = file_object_guard_cleanup(&vfs_object_guards, client);
    }
    cursor = (cursor + 1U) % FILE_OBJECT_GUARD_CAPACITY;
    vfs_operation_end();
    return result;
#else
    (void)now_ms; return 0;
#endif
}

int vfs_file_object_guard_fenced(uint32_t* mask) {
    if (!mask) return -REIST_EINVAL;
#ifdef VFS_FILE_OBJECT_GUARD
    if (!vfs_guard_initialized) { *mask = 0; return 0; }
    if (!vfs_guard_try_begin()) return -REIST_EBUSY;
    int result = file_object_guard_fenced(&vfs_object_guards, mask);
    vfs_operation_end();
    return result;
#else
    *mask = 0; return 0;
#endif
}

int vfs_file_object_guard_cancel_undelivered(
    const reist_file_object_guard_request_t* request,
    int service_pid, uint32_t service_generation) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (!request) return -REIST_EINVAL;
    if (!vfs_operation_begin()) return -REIST_EBUSY;
    int result = file_object_guard_cancel_undelivered(&vfs_object_guards,
        request->operation, request->token,
        (file_object_owner_t){service_pid, service_generation},
        (file_object_owner_t){request->client_pid, request->client_generation});
    vfs_operation_end();
    return result;
#else
    (void)request; (void)service_pid; (void)service_generation;
    return -REIST_ENOTSUP;
#endif
}

int vfs_file_object_guard_io_begin(uint32_t resource, uint32_t sector,
                                  bool flush, int service_pid,
                                  uint32_t service_generation) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (!vfs_operation_begin()) return -REIST_EBUSY;
    drive_t* drive = vfs_guard_platform_drive(resource);
    int result = !drive || !vfs_guard_physical(drive) ? -REIST_ENODEV : 0;
    if (!result && !vfs_guard_platform_live(service_pid, service_generation)) result = -REIST_EACCES;
    vfs_filesystem_t* selected = NULL;
    uint64_t first = drive && drive->type == DRIVE_TYPE_PARTITION ? drive->lba_offset : 0;
    uint64_t length = vfs_guard_drive_size(drive);
    if (!result && !flush && sector >= length) result = -REIST_EINVAL;
    for (vfs_mount_t* m = mount_list; !result && m; m = m->next) {
        if (!vfs_guard_same_physical(drive, m->fs->drive)) continue;
        uint32_t other_first, other_count;
        result = vfs_guard_extent(m->fs, &other_first, &other_count);
        if (result) break;
        if (first >= (uint64_t)other_first + other_count ||
            other_first >= first + length) continue;
        if (selected || (m->fs->drive != drive &&
            (first != other_first || length != other_count))) { result = -REIST_EINVAL; break; }
        if (!flush && (first + sector < other_first ||
            first + sector >= (uint64_t)other_first + other_count)) { result = -REIST_EINVAL; break; }
        selected = m->fs;
    }
    uint32_t canonical = resource;
    if (!result && selected) result = vfs_guard_resource(selected, &canonical);
    if (!result) result = file_object_guard_mutation_authorized(&vfs_object_guards,
        (file_object_owner_t){service_pid, service_generation}, canonical, vfs_guard_platform_now());
    if (result == -REIST_EACCES && vfs_guard_platform_live(service_pid, service_generation)) {
        /* No active reservation: only the already exclusive maintenance path
         * (or an unmounted medium) may issue raw effects. Plain BLOCK_WRITE
         * cannot bypass the two open-object registries. */
        if (!selected || (selected->maintenance_blocked && !selected->open_nodes)) {
            uint32_t pins = 0;
            int counted = selected ? vfs_guard_pin_count(selected, &pins) : VFS_OK;
            if (counted == VFS_OK && !pins)
                result = file_object_guard_legacy_enter(&vfs_object_guards, vfs_guard_platform_now());
            else result = -REIST_EBUSY;
        }
    }
    if (result) vfs_operation_end();
    return result;
#else
    (void)resource; (void)sector; (void)flush; (void)service_pid; (void)service_generation;
    return -REIST_ENOTSUP;
#endif
}

void vfs_file_object_guard_io_end(void) { vfs_operation_end(); }

#ifdef VFS_FILE_OBJECT_GUARD
/* Preflight never owns the metadata lock while waiting for VFS. Recheck the
 * exact token afterwards; the snapshot cannot renew the reservation. */
static bool vfs_journal_operation_begin(uint64_t deadline_ms) {
    uint64_t now = vfs_guard_platform_now();
    if (!deadline_ms || now >= deadline_ms ||
        deadline_ms - now > FILE_OBJECT_GUARD_MAX_MS) return false;
#ifndef KERNEL_HOST_TEST
    KASSERT_NOT_IRQ();
    if (kernel_mutex_lock_until(&vfs_operation_mutex, deadline_ms) != 0) return false;
#endif
    if (vfs_guard_platform_now() >= deadline_ms || vfs_guard_apply_media_changes()) {
        vfs_operation_end();
        return false;
    }
    return true;
}

/* No maintenance/parent alias fallback. The selected mount must name the
 * canonical resource, and all aliases must agree on extent and filesystem. */
static int vfs_journal_mount(uint32_t resource, vfs_filesystem_t** selected) {
    drive_t* drive = vfs_guard_platform_drive(resource);
    const drive_t* physical = vfs_guard_physical(drive);
    if (!physical || physical->type != DRIVE_TYPE_ATA) return -REIST_ENOTSUP;
    *selected = NULL;
    for (vfs_mount_t* m = mount_list; m; m = m->next) {
        if (m->fs->drive != drive) continue;
        uint32_t canonical;
        int result = vfs_guard_resource(m->fs, &canonical);
        if (result) return result;
        if (canonical != resource || !vfs_guard_kind(m->fs, REIST_FILE_OBJECT_FAT32))
            return -REIST_EINVAL;
        if (m->fs->object_media_revoked || !vfs_guard_platform_available(resource))
            return -REIST_EIO;
        if (m->fs->maintenance_blocked || m->fs->open_nodes) return -REIST_EBUSY;
        if (!m->fs->ops->journal_handoff || !m->fs->ops->journal_write_range)
            return -REIST_ENOTSUP;
        *selected = m->fs;
    }
    return *selected ? 0 : -REIST_ENOTSUP;
}
#endif

int vfs_storage_journal_io_begin(const reist_storage_journal_request_t* request,
    int pid, uint32_t generation, bool* was_pending, uint64_t* deadline_ms) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (was_pending) *was_pending = false;
    if (deadline_ms) *deadline_ms = 0;
    if (!was_pending || !deadline_ms || !file_object_guard_journal_request_valid(request))
        return -REIST_EINVAL;
    if (!vfs_guard_platform_live(pid, generation)) return -REIST_EACCES;
    uint64_t reserved_until = 0;
    int result = file_object_guard_journal_io_deadline(&vfs_object_guards,
        (file_object_owner_t){pid, generation}, request->token, request->resource,
        FILE_OBJECT_JOURNAL_CHECK, vfs_guard_platform_now(), NULL, &reserved_until);
    if (result) return result;
    if (!vfs_journal_operation_begin(reserved_until)) return -REIST_EBUSY;
    result = vfs_guard_platform_live(pid, generation) ? 0 : -REIST_EACCES;
    vfs_filesystem_t* fs = NULL;
    if (!result) result = vfs_journal_mount(request->resource, &fs);
    if (!result && request->operation != REIST_STORAGE_JOURNAL_FLUSH) {
        uint32_t first, count;
        result = fs->ops->volume_extent(fs, &first, &count);
        if (result || request->sector < first || request->sector - first >= count ||
            request->count > count - (request->sector - first)) result = -REIST_EINVAL;
        if (!result && request->operation == REIST_STORAGE_JOURNAL_WRITE_DEFERRED &&
            !fs->ops->journal_write_range(fs, request->sector, request->count))
            result = -REIST_EACCES;
    }
    if (!result) result = file_object_guard_journal_io_deadline(&vfs_object_guards,
        (file_object_owner_t){pid, generation}, request->token, request->resource,
        FILE_OBJECT_JOURNAL_CHECK, vfs_guard_platform_now(), was_pending, deadline_ms);
    if (result) vfs_operation_end();
    return result;
#else
    (void)request; (void)pid; (void)generation; (void)was_pending; (void)deadline_ms;
    return -REIST_ENOTSUP;
#endif
}

int vfs_storage_journal_io_complete(const reist_storage_journal_request_t* request,
    int pid, uint32_t generation, bool success) {
#ifdef VFS_FILE_OBJECT_GUARD
    /* VFS lock is still held. A failed or late operation retires UNKNOWN;
     * failure cannot be converted to NO_EFFECT by a later userspace finish. */
    if (!success) return file_object_guard_end(&vfs_object_guards, request->token,
        (file_object_owner_t){pid, generation}, REIST_FILE_OBJECT_UNKNOWN,
        vfs_guard_platform_now());
    return file_object_guard_journal_io(&vfs_object_guards,
        (file_object_owner_t){pid, generation}, request->token, request->resource,
        request->operation == REIST_STORAGE_JOURNAL_FLUSH ?
            FILE_OBJECT_JOURNAL_FLUSHED : FILE_OBJECT_JOURNAL_CHECK,
        vfs_guard_platform_now(), NULL);
#else
    (void)request; (void)pid; (void)generation; (void)success;
    return -REIST_ENOTSUP;
#endif
}

/* Called after the entire input was staged, before the first device effect. */
int vfs_storage_journal_io_mark_write(const reist_storage_journal_request_t* request,
    int pid, uint32_t generation) {
#ifdef VFS_FILE_OBJECT_GUARD
    return file_object_guard_journal_io(&vfs_object_guards,
        (file_object_owner_t){pid, generation}, request->token, request->resource,
        FILE_OBJECT_JOURNAL_WRITE, vfs_guard_platform_now(), NULL);
#else
    (void)request; (void)pid; (void)generation;
    return -REIST_ENOTSUP;
#endif
}

int vfs_file_object_guard_request(reist_file_object_guard_request_t* request,
                                  int service_pid, uint32_t service_generation) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (!file_object_guard_request_valid(request)) return -REIST_EINVAL;
    bool journal_begin = request->operation == REIST_FILE_OBJECT_MUTATION_BEGIN &&
                         (request->flags & REIST_FILE_OBJECT_EXTERNAL_JOURNAL);
    if (journal_begin ? !vfs_journal_operation_begin(request->deadline_ms) :
                       !vfs_operation_begin()) return -REIST_EBUSY;
    int result = 0;
    uint64_t now = vfs_guard_platform_now();
    file_object_owner_t service = {service_pid, service_generation};
    file_object_owner_t client = {request->client_pid, request->client_generation};
    if (!vfs_guard_platform_live(service_pid, service_generation)) result = -REIST_EACCES;
    uint32_t operation = request->operation;
    if (!result && (operation == REIST_FILE_OBJECT_PIN || operation == REIST_FILE_OBJECT_VERIFY) &&
        !vfs_guard_platform_live(client.pid, client.generation)) result = -REIST_ESTALE;
    reist_file_object_key_t keys[2] = {request->keys[0], request->keys[1]};
    uint32_t count = keys[1].kind ? 2U : 1U;
    if (!result && (operation == REIST_FILE_OBJECT_PIN || operation == REIST_FILE_OBJECT_MUTATION_BEGIN)) {
        for (uint32_t i = 0; !result && i < count; ++i) {
            result = vfs_guard_normalize(&keys[i]);
            if (!result && !vfs_guard_platform_available(keys[i].resource)) result = -REIST_EIO;
        }
        if (!result && count == 2 && keys[0].resource != keys[1].resource) result = -REIST_EINVAL;
    }
    if (!result && operation == REIST_FILE_OBJECT_MUTATION_BEGIN) {
        bool exclusive = (request->flags & REIST_FILE_OBJECT_EXCLUSIVE) != 0;
        if (exclusive) {
            for (vfs_mount_t* m = mount_list; !result && m; m = m->next) {
                uint32_t resource;
                result = vfs_guard_resource(m->fs, &resource);
                if (!result && resource == keys[0].resource && m->fs->open_nodes) result = -REIST_EBUSY;
            }
        }
        for (uint32_t i = 0; !result && i < VFS_OPEN_NODE_CAPACITY; ++i) {
            if (!open_node_records[i].node) continue;
            reist_file_object_key_t legacy;
            result = vfs_guard_node_key(open_node_records[i].node, &legacy);
            for (uint32_t j = 0; !result && j < count; ++j)
                if (!memcmp(&legacy, &keys[j], sizeof(legacy))) result = -REIST_EBUSY;
        }
    }
    if (!result) switch (operation) {
    case REIST_FILE_OBJECT_SNAPSHOT:
        result = file_object_guard_snapshot(&vfs_object_guards, &request->epoch, now);
        break;
    case REIST_FILE_OBJECT_PIN:
        result = file_object_guard_pin(&vfs_object_guards, &keys[0], service, client,
                                        request->epoch, now, &request->token);
        if (!result && !vfs_guard_platform_live(client.pid, client.generation)) {
            int released = file_object_guard_cancel_undelivered(&vfs_object_guards,
                operation, request->token, service, client);
            result = released ? released : -REIST_ESTALE;
            request->token = 0;
        }
        break;
    case REIST_FILE_OBJECT_RELEASE:
        result = file_object_guard_release(&vfs_object_guards, request->token, service, client);
        break;
    case REIST_FILE_OBJECT_VERIFY:
        result = file_object_guard_lookup(&vfs_object_guards, request->token, service, client, now, &keys[0]);
        if (!result && !vfs_guard_platform_available(keys[0].resource)) result = -REIST_EIO;
        break;
    case REIST_FILE_OBJECT_MUTATION_BEGIN:
        result = file_object_guard_begin_mode(&vfs_object_guards, keys, count,
            request->flags, service,
            request->epoch, now, request->deadline_ms, &request->token);
        if (!result && (request->flags & REIST_FILE_OBJECT_EXTERNAL_JOURNAL)) {
            vfs_filesystem_t* selected = NULL;
            result = vfs_journal_mount(keys[0].resource, &selected);
            if (!result && request->keys[0].resource != keys[0].resource) result = -REIST_EINVAL;
            if (!result) result = selected->ops->journal_handoff(selected, request->deadline_ms);
            for (vfs_mount_t* m = mount_list; !result && m; m = m->next) {
                if (m->fs == selected || !vfs_guard_same_physical(selected->drive, m->fs->drive)) continue;
                uint32_t canonical;
                result = vfs_guard_resource(m->fs, &canonical);
                if (!result && canonical == keys[0].resource)
                    result = m->fs->ops->journal_handoff ?
                        m->fs->ops->journal_handoff(m->fs, request->deadline_ms) : -REIST_ENOTSUP;
            }
            if (!result) result = file_object_guard_journal_io(&vfs_object_guards,
                service, request->token, keys[0].resource, FILE_OBJECT_JOURNAL_CHECK,
                vfs_guard_platform_now(), NULL);
            if (result) {
                int cancelled = file_object_guard_cancel_undelivered(&vfs_object_guards,
                    operation, request->token, service, client);
                if (cancelled) result = cancelled;
                request->token = 0;
            }
        }
        break;
    case REIST_FILE_OBJECT_MUTATION_END:
        result = file_object_guard_end(&vfs_object_guards, request->token, service, request->flags, now);
        break;
    default: result = -REIST_EINVAL; break;
    }
    /* Keys in the caller's request remain in their original resource namespace.
     * Only admitted output epoch/token fields change. */
    vfs_operation_end();
    return result;
#else
    (void)request; (void)service_pid; (void)service_generation;
    return -REIST_ENOTSUP;
#endif
}

#ifdef KERNEL_HOST_TEST
__attribute__((weak)) bool vfs_host_mutation_begin(void) {
    return true;
}

__attribute__((weak)) bool vfs_host_mutation_end(bool commit) {
    (void)commit;
    return true;
}
#endif

static bool vfs_mutation_begin(void) {
#ifdef VFS_FILE_OBJECT_GUARD
    vfs_guard_mutation_error = file_object_guard_legacy_enter(
        &vfs_object_guards, vfs_guard_platform_now());
    if (vfs_guard_mutation_error) return false;
#endif
#ifndef KERNEL_HOST_TEST
    return filesystem_mutation_begin(pit_monotonic_ms());
#else
    return vfs_host_mutation_begin();
#endif
}

static int vfs_mutation_denied(void) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (vfs_guard_mutation_error) return vfs_guard_errno(vfs_guard_mutation_error);
#endif
    return VFS_ERR_READ_ONLY;
}

static bool vfs_mutation_end(bool commit) {
#ifndef KERNEL_HOST_TEST
    return filesystem_mutation_end(commit);
#else
    return vfs_host_mutation_end(commit);
#endif
}

static int vfs_mutation_finish(bool armed, int result) {
#ifndef KERNEL_HOST_TEST
    if (result == VFS_ERR_IO) filesystem_fence_mutations();
#endif
    if (armed && !vfs_mutation_end(result != VFS_ERR_IO)) return VFS_ERR_IO;
    return result;
}

static bool vfs_valid_absolute_path(const char* path) {
    if (!path || path[0] != '/') return false;
    size_t length = strlen(path);
    if (length == 0 || length > 255) return false;
    for (size_t i = 1; i < length; i++) {
        if (path[i] == '/' && path[i - 1] == '/') return false;
        if (path[i - 1] == '/' && path[i] == '.' &&
            (path[i + 1] == '\0' || path[i + 1] == '/' ||
             (path[i + 1] == '.' &&
              (path[i + 2] == '\0' || path[i + 2] == '/')))) {
            return false;
        }
    }
    return true;
}

// ===========================================================================
// VFS Initialization
// ===========================================================================

static void vfs_init_locked(void) {
#ifdef VFS_FILE_OBJECT_GUARD
    if (file_object_guard_init(&vfs_object_guards) != 0) return;
    vfs_guard_initialized = true;
#endif
    printf("VFS: Initializing Virtual File System...\n");

    if (mount_list != NULL) {
        printf("VFS: Already initialized with active mounts.\n");
        return;
    }
    
    // Clear registration table
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        registered_filesystems[i].registered = false;
    }
    
    mount_list = NULL;
    fs_count = 0;
    mount_count = 0;
    memset(open_node_records, 0, sizeof(open_node_records));
    
    printf("VFS: Initialization complete.\n");
}

// ===========================================================================
// Filesystem Registration
// ===========================================================================

static int vfs_register_filesystem_locked(const char* name,
                                          vfs_filesystem_ops_t* ops) {
    if (fs_count >= MAX_FILESYSTEMS) {
        printf("VFS: Error - maximum filesystems registered.\n");
        return VFS_ERR_NO_MEMORY;
    }
    
    if (!name || !ops || name[0] == '\0' || strlen(name) > 31) {
        printf("VFS: Error - invalid parameters.\n");
        return VFS_ERR_INVALID;
    }
    
    // Check if already registered
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (registered_filesystems[i].registered &&
            strcmp(registered_filesystems[i].name, name) == 0) {
            printf("VFS: Filesystem '%s' already registered.\n", name);
            return VFS_ERR_EXISTS;
        }
    }
    
    // Find free slot
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (!registered_filesystems[i].registered) {
            strncpy(registered_filesystems[i].name, name, 31);
            registered_filesystems[i].name[31] = '\0';
            registered_filesystems[i].ops = ops;
            registered_filesystems[i].registered = true;
            fs_count++;
            printf("VFS: Registered filesystem '%s'\n", name);
            return VFS_OK;
        }
    }
    
    return VFS_ERR_NO_MEMORY;
}

// ===========================================================================
// Mount/Unmount Operations
// ===========================================================================

static int vfs_mount_locked(drive_t* drive, const char* fs_type,
                            const char* mount_path,
                            bool maintenance_blocked) {
    if (!drive || !fs_type || !mount_path) {
        return VFS_ERR_INVALID;
    }
    
    size_t mount_path_length = strlen(mount_path);
    if (!vfs_valid_absolute_path(mount_path) ||
        mount_path_length >= sizeof(drive->mount_point) ||
        (mount_path_length > 1 && mount_path[mount_path_length - 1] == '/')) {
        return VFS_ERR_INVALID;
    }

    if (mount_count >= MAX_MOUNTS) {
        return VFS_ERR_NO_MEMORY;
    }

    // A path can identify at most one mount.  Silently shadowing an existing
    // mount leaks state and makes unmount order-dependent.
    for (vfs_mount_t* current = mount_list; current; current = current->next) {
        if (strcmp(current->path, mount_path) == 0) {
            return VFS_ERR_EXISTS;
        }
        if (current->fs != NULL && current->fs->drive == drive) {
            return VFS_ERR_EXISTS;
        }
    }

    // Find registered filesystem
    vfs_filesystem_ops_t* ops = NULL;
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        if (registered_filesystems[i].registered &&
            strcmp(registered_filesystems[i].name, fs_type) == 0) {
            ops = registered_filesystems[i].ops;
            break;
        }
    }
    
    if (!ops || !ops->mount) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    // Allocate filesystem structure
    vfs_filesystem_t* fs = (vfs_filesystem_t*)malloc(sizeof(vfs_filesystem_t));
    if (!fs) {
        return VFS_ERR_NO_MEMORY;
    }
    
    strncpy(fs->name, fs_type, 31);
    fs->name[31] = '\0';
    fs->drive = drive;
    fs->ops = ops;
    fs->fs_data = NULL;
    fs->root = NULL;
    fs->open_nodes = 0;
    fs->maintenance_blocked = maintenance_blocked;
    fs->object_media_revoked = false;

    // Allocate the mount record before activating the filesystem so every
    // allocation failure is still side-effect free.
    vfs_mount_t* mount = (vfs_mount_t*)malloc(sizeof(vfs_mount_t));
    if (!mount) {
        free(fs);
        return VFS_ERR_NO_MEMORY;
    }
    
    // Call filesystem-specific mount
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_mount_admission(drive);
    if (admission == VFS_OK) admission = vfs_guard_errno(file_object_guard_legacy_enter(
        &vfs_object_guards, vfs_guard_platform_now()));
    if (admission != VFS_OK) { free(mount); free(fs); return admission; }
#endif
    int result = ops->mount(fs, drive);
    if (result != VFS_OK) {
        free(mount);
        free(fs);
        return result;
    }
    
    strncpy(mount->path, mount_path, 255);
    mount->path[255] = '\0';
    mount->fs = fs;
    mount->next = mount_list;
    mount_list = mount;
    mount_count++;
    strncpy(drive->mount_point, mount_path, sizeof(drive->mount_point) - 1U);
    drive->mount_point[sizeof(drive->mount_point) - 1U] = '\0';
    
    printf("VFS: Successfully mounted %s at %s\n", drive->name, mount_path);
    return VFS_OK;
}

static int vfs_unmount_locked(const char* mount_path) {
    if (!mount_path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_mount_t** current = &mount_list;
    while (*current) {
        if (strcmp((*current)->path, mount_path) == 0) {
            vfs_mount_t* to_remove = *current;

            if (to_remove->fs->open_nodes != 0) {
                return VFS_ERR_BUSY;
            }
#ifdef VFS_FILE_OBJECT_GUARD
            uint32_t pins = 0;
            int counted = vfs_guard_pin_count(to_remove->fs, &pins);
            if (counted != VFS_OK || pins) return counted != VFS_OK ? counted : VFS_ERR_BUSY;
            uint32_t resource;
            int revoked = vfs_guard_resource(to_remove->fs, &resource);
            if (!revoked) revoked = file_object_guard_revoke_media(&vfs_object_guards, resource);
            if (revoked) return vfs_guard_errno(revoked);
#endif
            
            // Unmount filesystem
            if (to_remove->fs->ops->unmount) {
                int result = to_remove->fs->ops->unmount(to_remove->fs);
                if (result != VFS_OK) {
                    return result;
                }
            }
            
            // Remove from list
            *current = to_remove->next;
            if (to_remove->fs->drive != NULL) {
                to_remove->fs->drive->mount_point[0] = '\0';
            }
            free(to_remove->fs);
            free(to_remove);
            mount_count--;
            
            printf("VFS: Unmounted %s\n", mount_path);
            return VFS_OK;
        }
        current = &(*current)->next;
    }
    
    return VFS_ERR_NOT_FOUND;
}

// ===========================================================================
// Path Resolution
// ===========================================================================

static vfs_filesystem_t* vfs_get_filesystem_locked(const char* path) {
    if (!vfs_valid_absolute_path(path)) {
        return NULL;
    }
    
    // Find longest matching mount point
    vfs_mount_t* best_match = NULL;
    size_t best_match_len = 0;
    size_t path_len = strlen(path);
    
    vfs_mount_t* current = mount_list;
    while (current) {
        size_t mount_len = strlen(current->path);
        bool is_root_mount = (mount_len == 1 && current->path[0] == '/');
        bool segment_boundary = mount_len <= path_len &&
                                (path[mount_len] == '\0' || path[mount_len] == '/');
        if (mount_len <= path_len &&
            strncmp(path, current->path, mount_len) == 0 &&
            (is_root_mount || segment_boundary)) {
            if (mount_len > best_match_len) {
                best_match = current;
                best_match_len = mount_len;
            }
        }
        current = current->next;
    }
    
    return best_match ? best_match->fs : NULL;
}

static const char* vfs_get_relative_path_locked(const char* absolute_path,
                                                vfs_filesystem_t* fs) {
    if (!absolute_path || !fs) {
        return NULL;
    }
    
    // Find the mount point for this filesystem
    vfs_mount_t* current = mount_list;
    while (current) {
        if (current->fs == fs) {
            size_t mount_len = strlen(current->path);
            if (strncmp(absolute_path, current->path, mount_len) == 0) {
                const char* relative = absolute_path + mount_len;
                return (*relative == '\0') ? "/" : relative;
            }
        }
        current = current->next;
    }
    
    return absolute_path;
}

// ===========================================================================
// File Operations
// ===========================================================================

static int vfs_open_locked(const char* path, vfs_node_t** node) {
    if (!path || !node) {
        return VFS_ERR_INVALID;
    }
    *node = NULL;
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    if (fs->maintenance_blocked) return VFS_ERR_BUSY;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->open) {
        return VFS_ERR_UNSUPPORTED;
    }

    bool root_path = strcmp(relative_path, "/") == 0 ||
                     relative_path[0] == '\0';
    int record_slot = root_path ? -1 : vfs_open_record_free_slot();
    if (!root_path && record_slot < 0) return VFS_ERR_BUSY;
    
    int result = fs->ops->open(fs, relative_path, node);
    if (result != VFS_OK) {
        return result;
    }
    if (!*node || (*node)->fs != fs) {
        if (*node && fs->ops->close) {
            fs->ops->close(*node);
        }
        *node = NULL;
        return VFS_ERR_IO;
    }

    if (fs->open_nodes == UINT32_MAX) {
        if (fs->ops->close) fs->ops->close(*node);
        *node = NULL;
        return VFS_ERR_BUSY;
    }
    if (*node != fs->root) {
        if (record_slot < 0 || vfs_open_record_find(*node) >= 0) {
            if (fs->ops->close) fs->ops->close(*node);
            *node = NULL;
            return VFS_ERR_IO;
        }
        open_node_records[record_slot].fs = fs;
        open_node_records[record_slot].node = *node;
    }
    fs->open_nodes++;
    return VFS_OK;
}

static int vfs_close_locked(vfs_node_t* node) {
    if (!node || !node->fs) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = node->fs;
    if (!fs->ops->close) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    int record_slot = node == fs->root ? -1 : vfs_open_record_find(node);
    if (node != fs->root && record_slot < 0) return VFS_ERR_INVALID;
    if (fs->open_nodes == 0U) return VFS_ERR_INVALID;

    int result = fs->ops->close(node);
    if (result == VFS_OK && fs->open_nodes > 0) {
        if (record_slot >= 0) {
            open_node_records[record_slot].fs = NULL;
            open_node_records[record_slot].node = NULL;
        }
        fs->open_nodes--;
    }
    return result;
}

static int vfs_path_open_locked(vfs_filesystem_t* fs, const char* path,
                                bool* open) {
    if (!fs || !path || !open) return VFS_ERR_INVALID;
    *open = false;
    bool need_probe = vfs_filesystem_has_registered_nodes(fs);
#ifdef VFS_FILE_OBJECT_GUARD
    uint32_t pins = 0;
    int counted = vfs_guard_pin_count(fs, &pins);
    if (counted != VFS_OK) return counted;
    need_probe = need_probe || pins != 0;
#endif
    if (!need_probe) return VFS_OK;
    if (!fs->ops || !fs->ops->open || !fs->ops->close ||
        !fs->ops->same_object) return VFS_ERR_BUSY;

    vfs_node_t* probe = NULL;
    int result = fs->ops->open(fs, path, &probe);
    if (result == VFS_ERR_NOT_FOUND) return VFS_OK;
    if (result != VFS_OK) return result;
    if (!probe || probe->fs != fs) {
        if (probe) (void)fs->ops->close(probe);
        return VFS_ERR_IO;
    }

    for (uint32_t index = 0U; index < VFS_OPEN_NODE_CAPACITY; ++index) {
        if (open_node_records[index].fs == fs &&
            open_node_records[index].node != NULL &&
            fs->ops->same_object(open_node_records[index].node, probe)) {
            *open = true;
            break;
        }
    }
#ifdef VFS_FILE_OBJECT_GUARD
    if (!*open) {
        reist_file_object_key_t key;
        int checked = vfs_guard_node_key(probe, &key);
        if (!checked) checked = file_object_guard_key_busy(&vfs_object_guards, &key,
                                                           vfs_guard_platform_now());
        if (checked == -REIST_EBUSY) *open = true;
        else if (checked) result = vfs_guard_errno(checked);
    }
#endif
    return fs->ops->close(probe) == VFS_OK ? result : VFS_ERR_IO;
}

static int vfs_read_locked(vfs_node_t* node, uint32_t offset, uint32_t size,
                           uint8_t* buffer) {
    if (!node || !node->fs || !buffer) {
        return VFS_ERR_INVALID;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(node->fs);
    if (admission != VFS_OK) return admission;
#endif
    if (!node->fs->ops->read) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return node->fs->ops->read(node, offset, size, buffer);
}

static int vfs_write_locked(vfs_node_t* node, uint32_t offset, uint32_t size,
                            const uint8_t* buffer) {
    if (!node || !node->fs || !buffer) {
        return VFS_ERR_INVALID;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(node->fs);
    if (admission != VFS_OK) return admission;
#endif
    if (!node->fs->ops->write) {
        return VFS_ERR_READ_ONLY;
    }
    
    return node->fs->ops->write(node, offset, size, buffer);
}

static int vfs_truncate_locked(vfs_node_t* node, uint32_t size) {
    if (!node || !node->fs || !node->fs->ops) return VFS_ERR_INVALID;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(node->fs);
    if (admission != VFS_OK) return admission;
#endif
    if (node->type != VFS_FILE) return VFS_ERR_IS_DIR;
    if (!node->fs->ops->truncate) return VFS_ERR_UNSUPPORTED;
    return node->fs->ops->truncate(node, size);
}

static int vfs_fstat_locked(vfs_node_t* node, vfs_dir_entry_t* stat) {
    if (!node || !node->fs || !node->fs->ops || !stat)
        return VFS_ERR_INVALID;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(node->fs);
    if (admission != VFS_OK) return admission;
#endif
    if (!node->fs->ops->fstat) return VFS_ERR_UNSUPPORTED;
    return node->fs->ops->fstat(node, stat);
}

// ===========================================================================
// Directory Operations
// ===========================================================================

static int vfs_readdir_locked(const char* path, uint32_t index,
                              vfs_dir_entry_t* entry) {
    if (!path || !entry) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    // Open directory node
    vfs_node_t* dir_node;
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    
    if (!fs->ops->open) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    int result = fs->ops->open(fs, relative_path, &dir_node);
    if (result != VFS_OK) {
        return result;
    }
    
    if (dir_node->type != VFS_DIRECTORY) {
        if (fs->ops->close) {
            fs->ops->close(dir_node);
        }
        return VFS_ERR_NOT_DIR;
    }
    
    if (!fs->ops->readdir) {
        if (fs->ops->close) {
            fs->ops->close(dir_node);
        }
        return VFS_ERR_UNSUPPORTED;
    }
    
    result = fs->ops->readdir(dir_node, index, entry);
    
    if (fs->ops->close) {
        fs->ops->close(dir_node);
    }
    
    return result;
}

static int vfs_sync_locked(vfs_node_t* node) {
    if (!node || !node->fs || !node->fs->ops) return VFS_ERR_INVALID;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(node->fs);
    if (admission != VFS_OK) return admission;
#endif
    if (!node->fs->ops->sync) return VFS_ERR_UNSUPPORTED;
    return node->fs->ops->sync(node);
}

static int vfs_readdir_batch_locked(const char* path, uint32_t index,
                                    vfs_dir_entry_t* entries,
                                    uint32_t capacity) {
    if (!path || !entries || capacity == 0) return VFS_ERR_INVALID;
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs || !fs->ops->open) return VFS_ERR_NOT_FOUND;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif

    vfs_node_t* node = NULL;
    int result = fs->ops->open(
        fs, vfs_get_relative_path_locked(path, fs), &node);
    if (result != VFS_OK) return result;
    if (!node || node->type != VFS_DIRECTORY) {
        result = VFS_ERR_NOT_DIR;
    } else if (fs->ops->readdir_batch) {
        result = fs->ops->readdir_batch(node, index, entries, capacity);
    } else if (!fs->ops->readdir) {
        result = VFS_ERR_UNSUPPORTED;
    } else {
        uint32_t count = 0;
        while (count < capacity) {
            int current = fs->ops->readdir(node, index + count,
                                           &entries[count]);
            if (current == VFS_ERR_NOT_FOUND) break;
            if (current != VFS_OK) { result = current; goto close_node; }
            ++count;
        }
        result = (int)count;
    }

close_node:
    if (node && fs->ops->close) (void)fs->ops->close(node);
    return result;
}

static int vfs_mkdir_locked(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->mkdir) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->mkdir(fs, relative_path);
}

static int vfs_rmdir_locked(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->rmdir) {
        return VFS_ERR_UNSUPPORTED;
    }

    bool open = false;
    int lock_result = vfs_path_open_locked(fs, relative_path, &open);
    if (lock_result != VFS_OK) return lock_result;
    if (open) return VFS_ERR_BUSY;
    
    return fs->ops->rmdir(fs, relative_path);
}

static int vfs_space_locked(const char* path, vfs_space_info_t* info) {
    if (!path || !info) return VFS_ERR_INVALID;
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) return VFS_ERR_NOT_FOUND;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    if (!fs->ops->space) return VFS_ERR_UNSUPPORTED;
    return fs->ops->space(fs, info);
}

// ===========================================================================
// File Management
// ===========================================================================

static int vfs_create_locked(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->create) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->create(fs, relative_path);
}

static int vfs_delete_locked(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->delete) {
        return VFS_ERR_UNSUPPORTED;
    }

    bool open = false;
    int lock_result = vfs_path_open_locked(fs, relative_path, &open);
    if (lock_result != VFS_OK) return lock_result;
    if (open) return VFS_ERR_BUSY;
    
    return fs->ops->delete(fs, relative_path);
}

static int vfs_stat_locked(const char* path, vfs_dir_entry_t* stat) {
    if (!path || !stat) {
        return VFS_ERR_INVALID;
    }
    
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) {
        return VFS_ERR_NOT_FOUND;
    }
    
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->stat) {
        return VFS_ERR_UNSUPPORTED;
    }
    
    return fs->ops->stat(fs, relative_path, stat);
}

static int vfs_rename_locked(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return VFS_ERR_INVALID;

    vfs_filesystem_t* old_fs = vfs_get_filesystem_locked(old_path);
    vfs_filesystem_t* new_fs = vfs_get_filesystem_locked(new_path);
    if (!old_fs || !new_fs) return VFS_ERR_NOT_FOUND;
    if (old_fs != new_fs) return VFS_ERR_UNSUPPORTED;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(old_fs);
    if (admission != VFS_OK) return admission;
#endif
    if (!old_fs->ops->rename) return VFS_ERR_UNSUPPORTED;

    const char* old_relative = vfs_get_relative_path_locked(old_path, old_fs);
    const char* new_relative = vfs_get_relative_path_locked(new_path, new_fs);
    bool open = false;
    int lock_result = vfs_path_open_locked(old_fs, old_relative, &open);
    if (lock_result != VFS_OK) return lock_result;
    if (open) return VFS_ERR_BUSY;
    lock_result = vfs_path_open_locked(new_fs, new_relative, &open);
    if (lock_result != VFS_OK) return lock_result;
    if (open) return VFS_ERR_BUSY;
    return old_fs->ops->rename(old_fs, old_relative, new_relative);
}

static int vfs_touch_locked(const char* path) {
    if (!path) return VFS_ERR_INVALID;
    vfs_filesystem_t* fs = vfs_get_filesystem_locked(path);
    if (!fs) return VFS_ERR_NOT_FOUND;
#ifdef VFS_FILE_OBJECT_GUARD
    int admission = vfs_guard_open_check(fs);
    if (admission != VFS_OK) return admission;
#endif
    const char* relative_path = vfs_get_relative_path_locked(path, fs);
    if (!fs->ops->touch) return VFS_ERR_UNSUPPORTED;
    return fs->ops->touch(fs, relative_path);
}

// ===========================================================================
// Public serialized API
// ===========================================================================

void vfs_init(void) {
    if (!vfs_operation_begin()) return;
    vfs_init_locked();
    vfs_operation_end();
}

int vfs_register_filesystem(const char* name, vfs_filesystem_ops_t* ops) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_register_filesystem_locked(name, ops);
    vfs_operation_end();
    return result;
}

int vfs_mount(drive_t* drive, const char* fs_type, const char* mount_path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_mount_locked(drive, fs_type, mount_path, false);
    vfs_operation_end();
    return result;
}

int vfs_mount_maintenance(drive_t* drive, const char* fs_type,
                          const char* mount_path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_mount_locked(drive, fs_type, mount_path, true);
    vfs_operation_end();
    return result;
}

int vfs_unmount(const char* mount_path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_unmount_locked(mount_path) : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

static int vfs_maintenance_acquire_locked(drive_t* drive) {
    if (!drive) return VFS_ERR_INVALID;
    for (vfs_mount_t* mount = mount_list; mount; mount = mount->next) {
        if (mount->fs->drive != drive) continue;
        if (mount->fs->maintenance_blocked || mount->fs->open_nodes != 0U)
            return VFS_ERR_BUSY;
#ifdef VFS_FILE_OBJECT_GUARD
        uint32_t pins = 0;
        int counted = vfs_guard_pin_count(mount->fs, &pins);
        if (counted != VFS_OK || pins) return counted != VFS_OK ? counted : VFS_ERR_BUSY;
#endif
        mount->fs->maintenance_blocked = true;
        return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

static int vfs_maintenance_begin_locked(drive_t* drive) {
    if (!drive) return VFS_ERR_INVALID;
    for (vfs_mount_t* mount = mount_list; mount; mount = mount->next) {
        if (mount->fs->drive != drive) continue;
        mount->fs->maintenance_blocked = true;
        return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

static int vfs_maintenance_open_count_locked(drive_t* drive,
                                             uint32_t* open_nodes) {
    if (!drive || !open_nodes) return VFS_ERR_INVALID;
    for (vfs_mount_t* mount = mount_list; mount; mount = mount->next) {
        if (mount->fs->drive != drive) continue;
        if (!mount->fs->maintenance_blocked) return VFS_ERR_INVALID;
        *open_nodes = mount->fs->open_nodes;
#ifdef VFS_FILE_OBJECT_GUARD
        uint32_t pins = 0;
        int counted = vfs_guard_pin_count(mount->fs, &pins);
        if (counted != VFS_OK || UINT32_MAX - *open_nodes < pins) return VFS_ERR_BUSY;
        *open_nodes += pins;
#endif
        return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

static int vfs_maintenance_release_locked(drive_t* drive) {
    if (!drive) return VFS_ERR_INVALID;
    for (vfs_mount_t* mount = mount_list; mount; mount = mount->next) {
        if (mount->fs->drive != drive) continue;
        if (!mount->fs->maintenance_blocked) return VFS_OK;
        mount->fs->maintenance_blocked = false;
        return VFS_OK;
    }
    return VFS_ERR_NOT_FOUND;
}

int vfs_maintenance_acquire(drive_t* drive) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_maintenance_acquire_locked(drive);
    vfs_operation_end();
    return result;
}

int vfs_maintenance_begin(drive_t* drive) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_maintenance_begin_locked(drive);
    vfs_operation_end();
    return result;
}

int vfs_maintenance_open_count(drive_t* drive, uint32_t* open_nodes) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_maintenance_open_count_locked(drive, open_nodes);
    vfs_operation_end();
    return result;
}

int vfs_maintenance_release(drive_t* drive) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_maintenance_release_locked(drive);
    vfs_operation_end();
    return result;
}

int vfs_get_mount_info(drive_t* drive, vfs_mount_info_t* info) {
    if (!info) return VFS_ERR_INVALID;
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    memset(info, 0, sizeof(*info));
    int result = VFS_ERR_NOT_FOUND;
    if (drive != NULL) {
        for (vfs_mount_t* mount = mount_list; mount; mount = mount->next) {
            if (mount->fs->drive != drive) continue;
            info->mounted = true;
            info->maintenance_blocked = mount->fs->maintenance_blocked;
            info->open_nodes = mount->fs->open_nodes;
            strncpy(info->path, mount->path, sizeof(info->path) - 1U);
            strncpy(info->fs_type, mount->fs->name,
                    sizeof(info->fs_type) - 1U);
            result = VFS_OK;
            break;
        }
    }
    vfs_operation_end();
    return result;
}

int vfs_open(const char* path, vfs_node_t** node) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_open_locked(path, node);
    vfs_operation_end();
    return result;
}

int vfs_close(vfs_node_t* node) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_close_locked(node);
    vfs_operation_end();
    return result;
}

int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size,
             uint8_t* buffer) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_read_locked(node, offset, size, buffer);
    vfs_operation_end();
    return result;
}

int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size,
              const uint8_t* buffer) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_write_locked(node, offset, size, buffer)
                       : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

uint32_t vfs_write_chunk_capacity(vfs_node_t* node, uint32_t offset) {
    if (!vfs_operation_begin()) return VFS_DEFAULT_WRITE_CHUNK_CAPACITY;
    uint32_t capacity = VFS_DEFAULT_WRITE_CHUNK_CAPACITY;
    if (node != NULL && node->type == VFS_FILE && node->fs != NULL &&
        node->fs->ops != NULL &&
        node->fs->ops->write_chunk_capacity != NULL) {
        uint32_t candidate =
            node->fs->ops->write_chunk_capacity(node, offset);
        if (candidate >= VFS_DEFAULT_WRITE_CHUNK_CAPACITY &&
            candidate <= VFS_MAX_WRITE_CHUNK_CAPACITY)
            capacity = candidate;
    }
    vfs_operation_end();
    return capacity;
}

int vfs_truncate(vfs_node_t* node, uint32_t size) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_truncate_locked(node, size)
                       : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_fstat(vfs_node_t* node, vfs_dir_entry_t* stat) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_fstat_locked(node, stat);
    vfs_operation_end();
    return result;
}

int vfs_sync(vfs_node_t* node) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_sync_locked(node);
#ifndef KERNEL_HOST_TEST
    if (result == VFS_ERR_IO) filesystem_fence_mutations();
#endif
    vfs_operation_end();
    return result;
}

int vfs_readdir(const char* path, uint32_t index, vfs_dir_entry_t* entry) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_readdir_locked(path, index, entry);
    vfs_operation_end();
    return result;
}

int vfs_readdir_batch(const char* path, uint32_t index,
                      vfs_dir_entry_t* entries, uint32_t capacity) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_readdir_batch_locked(path, index, entries, capacity);
    vfs_operation_end();
    return result;
}

int vfs_mkdir(const char* path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_mkdir_locked(path) : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_rmdir(const char* path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_rmdir_locked(path) : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_create(const char* path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_create_locked(path) : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_delete(const char* path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_delete_locked(path) : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_rename(const char* old_path, const char* new_path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_rename_locked(old_path, new_path)
                       : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_touch(const char* path) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    bool armed = vfs_mutation_begin();
    int result = armed ? vfs_touch_locked(path) : vfs_mutation_denied();
    result = vfs_mutation_finish(armed, result);
    vfs_operation_end();
    return result;
}

int vfs_stat(const char* path, vfs_dir_entry_t* stat) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_stat_locked(path, stat);
    vfs_operation_end();
    return result;
}

int vfs_space(const char* path, vfs_space_info_t* info) {
    if (!vfs_operation_begin()) return VFS_ERR_BUSY;
    int result = vfs_space_locked(path, info);
    vfs_operation_end();
    return result;
}

vfs_filesystem_t* vfs_get_filesystem(const char* path) {
    if (!vfs_operation_begin()) return NULL;
    vfs_filesystem_t* result = vfs_get_filesystem_locked(path);
    vfs_operation_end();
    return result;
}

const char* vfs_get_relative_path(const char* absolute_path,
                                  vfs_filesystem_t* fs) {
    if (!vfs_operation_begin()) return NULL;
    const char* result = vfs_get_relative_path_locked(absolute_path, fs);
    vfs_operation_end();
    return result;
}
