/**
 * @file kernel/proc/process.c
 * @brief Prozess-, Adressraum- und Ressourcenverwaltung.
 *
 * Layer: Ring-0 process subsystem.
 * Contract: PID und Generation identifizieren exakt eine Prozessinstanz.
 * Safety: Fehler werden vor sichtbaren Seiteneffekten abgewiesen; Arbeit und Speicher sind begrenzt.
 */
#include "kernel/proc/process.h"
#include "kernel/proc/terminal_input.h"
#include "kernel/proc/program_image.h"
#include <stdbool.h>

#include "arch/x86/include/interrupt.h"
#include "arch/x86/mm/paging.h"
#include "mm/kmalloc.h"
#include "lib/libc/string.h"
#include "lib/libc/stdio.h"
#include "lib/libc/stdlib.h"
#include "fs/vfs/vfs.h"
#include "drivers/bus/drives.h"
#include "kernel/init/prg.h"
#include "kernel/sched/scheduler.h"
#include "include/kernel/panic.h"
#include "include/kernel/storage_request_pool.h"
#include "include/kernel/admin_maintenance.h"
#include "include/kernel/component_control.h"
#include "include/kernel/device_domain.h"
#include "drivers/net/net_socket.h"
#include "drivers/net/tcp_socket.h"
#include "drivers/video/framebuffer.h"
#include "include/kernel/critical_object.h"

#define USER_PROGRAM_ADDRESS PROGRAM_V1_BASE
#define PROGRAM_REGION_SIZE PROGRAM_V1_REGION_SIZE
#define RESCUE_PROGRAM_CACHE_VERSION 1U
#define RESCUE_PROGRAM_CACHE_COUNT 11U
#define RESCUE_PROGRAM_CACHE_CAPACITY (224U * 1024U)
#define RESCUE_PROGRAM_POOL_CAPACITY (448U * 1024U)

typedef struct {
    const char *path;
} rescue_program_cache_t;

typedef struct {
    uint32_t ready;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint32_t generation;
} rescue_program_meta_t;

static rescue_program_cache_t rescue_program_cache[RESCUE_PROGRAM_CACHE_COUNT] = {
    {.path = "/bin/shell.prg"},
    {.path = "/sbin/devctl.prg"},
    {.path = "/sbin/mount.prg"},
    {.path = "/sbin/umount.prg"},
    {.path = "/sbin/svcctl.prg"},
    {.path = "/libexec/reist/storage.prg"},
    {.path = "/libexec/reist/reist.prg"},
    {.path = "/sbin/drives.prg"},
    {.path = "/bin/ls.prg"},
    {.path = "/bin/cat.prg"},
    {.path = "/sbin/chkdsk.prg"},
};
static uint8_t rescue_program_pool[RESCUE_PROGRAM_POOL_CAPACITY];
static critical_object_t rescue_program_meta[RESCUE_PROGRAM_CACHE_COUNT];
static bool rescue_cache_initialized;

typedef struct {
    const char *legacy;
    const char *canonical;
} program_path_alias_t;

static const program_path_alias_t program_path_aliases[] = {
    {"/SHELL.PRG", "/bin/shell.prg"},
    {"/LS.PRG", "/bin/ls.prg"},
    {"/CAT.PRG", "/bin/cat.prg"},
    {"/BASIC.PRG", "/bin/basic.prg"},
    {"/EDIT.PRG", "/bin/edit.prg"},
    {"/PWD.PRG", "/bin/pwd.prg"},
    {"/MKDIR.PRG", "/bin/mkdir.prg"},
    {"/RMDIR.PRG", "/bin/rmdir.prg"},
    {"/DEL.PRG", "/bin/del.prg"},
    {"/COPY.PRG", "/bin/copy.prg"},
    {"/ECHO.PRG", "/bin/echo.prg"},
    {"/CLS.PRG", "/bin/cls.prg"},
    {"/SYSINFO.PRG", "/sbin/sysinfo.prg"},
    {"/MEMINFO.PRG", "/sbin/meminfo.prg"},
    {"/CHKDSK.PRG", "/sbin/chkdsk.prg"},
    {"/FDISK.PRG", "/sbin/fdisk.prg"},
    {"/FORMAT.PRG", "/sbin/format.prg"},
    {"/PS.PRG", "/sbin/ps.prg"},
    {"/KILL.PRG", "/sbin/kill.prg"},
    {"/DRIVES.PRG", "/sbin/drives.prg"},
    {"/DEVCTL.PRG", "/sbin/devctl.prg"},
    {"/MOUNT.PRG", "/sbin/mount.prg"},
    {"/UMOUNT.PRG", "/sbin/umount.prg"},
    {"/SVCCTL.PRG", "/sbin/svcctl.prg"},
    {"/HELLO.PRG", "/usr/bin/hello.prg"},
    {"/REPEAT.PRG", "/usr/bin/repeat.prg"},
    {"/CALC.PRG", "/usr/bin/calc.prg"},
    {"/DATE.PRG", "/usr/bin/date.prg"},
    {"/UPTIME.PRG", "/usr/bin/uptime.prg"},
    {"/ASCII.PRG", "/usr/bin/ascii.prg"},
    {"/SAVE.PRG", "/usr/bin/save.prg"},
    {"/SPAWN.PRG", "/usr/bin/spawn.prg"},
    {"/DESKTOP.PRG", "/usr/gui/bin/desktop.prg"},
    {"/usr/bin/desktop.prg", "/usr/gui/bin/desktop.prg"},
    {"/CHILDEX.PRG", "/libexec/reist/childex.prg"},
    {"/FAULTDE.PRG", "/libexec/reist/faultde.prg"},
    {"/FAULTUD.PRG", "/libexec/reist/faultud.prg"},
    {"/FAULTPF.PRG", "/libexec/reist/faultpf.prg"},
    {"/FAULTSTK.PRG", "/libexec/reist/faultstk.prg"},
    {"/GTEST.PRG", "/libexec/reist/gtest.prg"},
    {"/REIST.PRG", "/libexec/reist/reist.prg"},
    {"/STORAGE.PRG", "/libexec/reist/storage.prg"},
    {"/SLEEPER.PRG", "/libexec/reist/sleeper.prg"},
    {"/SATAWR.PRG", "/libexec/reist/satawr.prg"},
};

static bool program_path_equal(const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    return left_length == right_length &&
           strncasecmp(left, right, left_length) == 0;
}

static void canonicalize_program_path(char path[PROCESS_PATH_MAX]) {
    for (size_t index = 0U;
         index < sizeof(program_path_aliases) / sizeof(program_path_aliases[0]);
         ++index) {
        const program_path_alias_t *alias = &program_path_aliases[index];
        if (!program_path_equal(path, alias->legacy) &&
            !program_path_equal(path, alias->canonical)) continue;
        strcpy(path, alias->canonical);
        return;
    }
}

_Static_assert(sizeof(rescue_program_meta_t) <= CRITICAL_OBJECT_MAX_PAYLOAD,
               "rescue cache metadata exceeds protected object capacity");

static bool rescue_meta_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(rescue_program_meta_t))
        return false;
    const rescue_program_meta_t *meta = payload;
    return meta->ready == 1U && meta->size != 0U &&
           meta->size <= RESCUE_PROGRAM_CACHE_CAPACITY &&
           meta->offset <= RESCUE_PROGRAM_POOL_CAPACITY - meta->size &&
           meta->crc32 != 0U && meta->generation != 0U;
}

static uint32_t rescue_crc32(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

_Static_assert(PROGRAM_V1_BASE == USER_BASE,
               "MYPR v1 base must match the user address-space base");
_Static_assert(USER_STACK_LOWER_GUARD >= USER_HEAP_TOP,
               "User stack guards must not overlap the user heap");
_Static_assert(USER_STACK_UPPER_GUARD + PAGE_SIZE == USER_TOP,
               "Upper user stack guard must terminate at USER_TOP");

static void program_load_root_details(uint32_t *identity,
                                      uint32_t *location) {
    if (identity == NULL || location == NULL) return;
    *identity = UINT32_MAX;
    *location = (uint32_t)(uint16_t)drive_count;
    for (short index = 0; index < drive_count; ++index) {
        drive_t *drive = &detected_drives[index];
        if (strcmp(drive->mount_point, "/") != 0) continue;
        *identity = (uint32_t)(uint16_t)index;
        *location = drive->lba_offset;
        return;
    }
}

static int load_program_file_uncached(const char *program_name,
                                      uint8_t **image_out) {
    panic_context_set("program-load", "program loader", "open",
                      program_name);
    if (image_out == NULL) {
        panic_context_set_result(-22, 0U, 0U);
        return -1;
    }
    *image_out = NULL;
    vfs_node_t* node = NULL;
    int result = vfs_open(program_name, &node);
    if (result != VFS_OK || !node) {
        uint32_t identity, location;
        program_load_root_details(&identity, &location);
        panic_context_set_result(result, identity, location);
        printf("Program load open failed: %s (%d)\n", program_name, result);
        return -1;
    }
    if (node->type != VFS_FILE || node->size < sizeof(program_header_t) ||
        node->size > PROGRAM_REGION_SIZE) {
        panic_context_set_result(-8, (uint32_t)node->type, node->size);
        printf("Program load metadata invalid: %s (type=%u, size=%u)\n",
               program_name, (uint32_t)node->type, node->size);
        (void)vfs_close(node);
        return -1;
    }

    uint32_t loaded_size = node->size;
    panic_context_set("program-load", "program loader", "allocate image",
                      program_name);
    uint8_t *image = (uint8_t*)k_malloc(loaded_size);
    if (image == NULL) {
        panic_context_set_result(-12, loaded_size, 0U);
        printf("Program load allocation failed: %s (%u bytes)\n",
               program_name, loaded_size);
        (void)vfs_close(node);
        return -1;
    }
    /* Read the image sequentially in one VFS operation.  Restarting a FAT
     * read every page would walk the cluster chain from its beginning for
     * each chunk and makes larger programs progressively slower to start. */
    panic_context_set("program-load", "program loader", "read image",
                      program_name);
    result = vfs_read(node, 0, loaded_size, image);
    if (result != (int)loaded_size) {
        panic_context_set_result(result, loaded_size, 0U);
        printf("Program load read failed: %s (%d/%u bytes)\n",
               program_name, result, loaded_size);
        (void)vfs_close(node);
        k_free(image);
        return -1;
    }
    int close_result = vfs_close(node);
    panic_context_set("program-load", "program loader", "validate image",
                      program_name);
    int validation_result = program_image_validate(
        image, loaded_size, PROGRAM_REGION_SIZE);
    if (close_result != VFS_OK || validation_result != 0) {
        panic_context_set_result(validation_result, (uint32_t)close_result,
                                 loaded_size);
        printf("Program load validation failed: %s (close=%d, image=%d)\n",
               program_name, close_result, validation_result);
        k_free(image);
        return -1;
    }
    *image_out = image;
    return (int)loaded_size;
}

static int load_program_file(const char *program_name, uint8_t **image_out) {
    if (program_name == NULL || image_out == NULL) return -1;
    *image_out = NULL;
    for (uint32_t index = 0U; index < RESCUE_PROGRAM_CACHE_COUNT; ++index) {
        rescue_program_cache_t *cache = &rescue_program_cache[index];
        if (!rescue_cache_initialized ||
            strcmp(program_name, cache->path) != 0) continue;
        rescue_program_meta_t meta;
        size_t length = 0U;
        if (critical_object_read(&rescue_program_meta[index],
                RESCUE_PROGRAM_CACHE_VERSION, &meta, sizeof(meta), &length,
                rescue_meta_valid) < 0 || length != sizeof(meta) ||
            rescue_crc32(rescue_program_pool + meta.offset,
                         meta.size) != meta.crc32) {
            printf("REIST_RESCUE CACHE_INTEGRITY_FAIL %s\n", program_name);
            return -1;
        }
        uint8_t *image = (uint8_t*)k_malloc(meta.size);
        if (image == NULL) return -1;
        memcpy(image, rescue_program_pool + meta.offset, meta.size);
        *image_out = image;
        panic_context_set("program-load", "resident rescue cache", "copy",
                          program_name);
        printf("REIST_RESCUE CACHE_EXEC %s\n", program_name);
        return (int)meta.size;
    }
    return load_program_file_uncached(program_name, image_out);
}

bool process_cache_rescue_programs(void) {
    if (rescue_cache_initialized) return true;
    uint32_t used = 0U;
    for (uint32_t index = 0U; index < RESCUE_PROGRAM_CACHE_COUNT; ++index) {
        rescue_program_cache_t *cache = &rescue_program_cache[index];
        uint8_t *image = NULL;
        int size = load_program_file_uncached(cache->path, &image);
        const bool capacity_failure = size > 0 &&
            ((uint32_t)size > RESCUE_PROGRAM_CACHE_CAPACITY ||
             used > RESCUE_PROGRAM_POOL_CAPACITY - (uint32_t)size);
        if (size <= 0 || capacity_failure || image == NULL) {
            if (capacity_failure) {
                panic_context_set("program-load", "resident rescue cache",
                                  "capacity", cache->path);
                panic_context_set_result(-28, used, (uint32_t)size);
            }
            printf("REIST_RESCUE CACHE_BUILD_FAIL path=%s size=%d used=%u "
                   "capacity=%u\n", cache->path, size, used,
                   RESCUE_PROGRAM_POOL_CAPACITY);
            if (image != NULL) k_free(image);
            memset(rescue_program_pool, 0, sizeof(rescue_program_pool));
            return false;
        }
        memcpy(rescue_program_pool + used, image, (size_t)size);
        rescue_program_meta_t meta = {
            .ready = 1U,
            .offset = used,
            .size = (uint32_t)size,
            .crc32 = rescue_crc32(rescue_program_pool + used, (size_t)size),
            .generation = 1U,
        };
        used += (uint32_t)size;
        k_free(image);
        if (critical_object_init(&rescue_program_meta[index],
                RESCUE_PROGRAM_CACHE_VERSION, &meta, sizeof(meta)) != 0)
            return false;
    }
    rescue_cache_initialized = true;
    printf("REIST_RESCUE CACHE_READY programs=%u capacity=%u\n",
           RESCUE_PROGRAM_CACHE_COUNT, RESCUE_PROGRAM_POOL_CAPACITY);
    return true;
}

static void release_admission_image(uint8_t **image) {
    if (image == NULL || *image == NULL) return;
    k_free(*image);
    *image = NULL;
}


static Process process_list[MAX_PROGRAMS];
static int next_pid = 1; // PID counter starting at 1
static spinlock_t process_table_lock = SPINLOCK_INIT;

uint32_t process_table_lock_irqsave(void) {
    return spinlock_acquire_irq(&process_table_lock);
}

void process_table_unlock_irqrestore(uint32_t flags) {
    spinlock_release_irq(&process_table_lock, flags);
}

spinlock_t *process_table_lock_ref(void) {
    return &process_table_lock;
}

bool process_table_lock_is_owned(void) {
    return spinlock_is_owned_by_current(&process_table_lock);
}

bool process_begin_exit(Process *process, uint32_t generation) {
    if (process == NULL || generation == 0U) return false;
    uint32_t flags = process_table_lock_irqsave();
    bool accepted = process->is_running && !process->terminating &&
                    process->generation == generation;
    if (accepted) {
        terminal_input_process_cleanup(process->pid, generation);
        process->terminating = true;
    }
    process_table_unlock_irqrestore(flags);
    return accepted;
}

int process_terminal_input(Process *caller,
                           const reist_terminal_input_request_t *request) {
    if (caller == NULL || request == NULL) return -22;
    uint32_t flags = process_table_lock_irqsave();
    int result = -3;
    if (caller->is_running && !caller->terminating && !caller->has_exited) {
        Process *target = NULL;
        if (request->operation == REIST_TERMINAL_TRANSFER) {
            for (uint32_t i = 0U; i < MAX_PROGRAMS; ++i) {
                Process *candidate = &process_list[i];
                if (candidate->is_running && !candidate->terminating &&
                    !candidate->has_exited && candidate->pid == request->target_pid &&
                    candidate->generation == request->target_generation) {
                    target = candidate;
                    break;
                }
            }
        }
        result = terminal_input_control_locked(caller, target, request);
    }
    process_table_unlock_irqrestore(flags);
    return result;
}

static void profile_allow(process_domain_profile_t *profile,
                          uint32_t syscall_index) {
    if (syscall_index >= PROCESS_DOMAIN_SYSCALL_LIMIT) return;
    profile->allowed_syscalls[syscall_index / 32U] |=
        1U << (syscall_index % 32U);
}

static bool initialize_domain_profile(process_domain_profile_t *profile,
                                      process_domain_kind_t kind) {
    if (profile == NULL) return false;
    memset(profile, 0, sizeof(*profile));
    profile->version = PROCESS_DOMAIN_PROFILE_VERSION;
    profile->struct_size = sizeof(*profile);
    profile->kind = (uint32_t)kind;
    if (kind == PROCESS_DOMAIN_COMPATIBILITY) {
        for (uint32_t index = 0; index < PROCESS_DOMAIN_SYSCALL_LIMIT; ++index) {
            if (index != SYS_FILE_OBJECT_GUARD) profile_allow(profile, index);
        }
        return true;
    }
    if (kind == PROCESS_DOMAIN_SCRIPT) {
        static const uint8_t script_syscalls[] = {
            SYS_MALLOC, SYS_FREE, SYS_REALLOC, SYS_EXIT, SYS_GETPID,
            SYS_PROCESS_INFO, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
            SYS_IPC_SEND_TIMEOUT, SYS_IPC_RECEIVE_TIMEOUT, SYS_IPC_RELEASE,
            SYS_PROCESS_IDENTITY, SYS_PROCESS_RESTRICT
        };
        for (size_t index = 0; index < sizeof(script_syscalls); ++index)
            profile_allow(profile, script_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_STORAGE) {
        static const uint8_t storage_syscalls[] = {
            SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
            SYS_DRIVE_INFO,
            SYS_STORAGE_BIND, SYS_STORAGE_CLAIM, SYS_STORAGE_BLOCK_READ,
            SYS_STORAGE_BLOCK_WRITE, SYS_STORAGE_MAINT_ACQUIRE,
            SYS_STORAGE_MAINT_RENEW, SYS_STORAGE_MAINT_RELEASE,
            SYS_STORAGE_COMPLETE, SYS_STORAGE_BLOCK_FLUSH,
            SYS_STORAGE_MEDIA_COMMIT, SYS_STORAGE_FORMAT_PROBE,
            SYS_BOOT_STATUS, SYS_STAT, SYS_PROCESS_IDENTITY,
            SYS_STORAGE_CLAIM_IDENTITY, SYS_STORAGE_BULK, SYS_FILE_OBJECT_GUARD
        };
        for (size_t index = 0;
             index < sizeof(storage_syscalls) / sizeof(storage_syscalls[0]);
             ++index) profile_allow(profile, storage_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_ADMIN) {
        static const uint8_t admin_syscalls[] = {
            0U, 1U, SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS,
            SYS_TERMINAL_WRITE, SYS_MONOTONIC_MS, SYS_DRIVE_INFO,
            SYS_DRIVE_STATUS, SYS_ADMIN_STORAGE, SYS_PARTITION_CREATE,
            SYS_STORAGE_SUBMIT, SYS_STORAGE_COLLECT, SYS_STORAGE_CANCEL,
            SYS_STORAGE_BULK
        };
        for (size_t index = 0;
             index < sizeof(admin_syscalls) / sizeof(admin_syscalls[0]);
             ++index) profile_allow(profile, admin_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_MAINTENANCE) {
        /* CHKDSK can submit the parser-authoritative read-only VFS envelope
         * and bounded FAT12 maintenance requests, but receives no direct
         * namespace, content, raw block, bulk, controller, DMA or port
         * authority. */
        static const uint8_t maintenance_syscalls[] = {
            0U, 1U, SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS,
            SYS_TERMINAL_WRITE, SYS_MONOTONIC_MS, SYS_DRIVE_INFO,
            SYS_STORAGE_SUBMIT, SYS_STORAGE_COLLECT, SYS_STORAGE_CANCEL
        };
        for (size_t index = 0;
             index < sizeof(maintenance_syscalls) /
                         sizeof(maintenance_syscalls[0]);
             ++index) profile_allow(profile, maintenance_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_COMPONENT_ADMIN) {
        static const uint8_t component_admin_syscalls[] = {
            0U, SYS_EXIT, SYS_GETPID, SYS_TERMINAL_WRITE,
            SYS_MONOTONIC_MS, SYS_COMPONENT_CONTROL
        };
        for (size_t index = 0;
             index < sizeof(component_admin_syscalls) /
                         sizeof(component_admin_syscalls[0]);
             ++index) profile_allow(profile, component_admin_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_DRIVER) {
        /* Device authority is granted separately through generation-scoped
         * capabilities. The launch profile starts with only lifecycle, time
         * and bounded IPC; it has no filesystem, network or display access. */
        static const uint8_t driver_syscalls[] = {
            SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
            SYS_IPC_CREATE, SYS_IPC_SEND, SYS_IPC_RECEIVE, SYS_IPC_CLOSE,
            SYS_IPC_SEND_TIMEOUT, SYS_IPC_RECEIVE_TIMEOUT,
            SYS_DEVICE_CONTROL, SYS_DISPLAY_CONTROL
        };
        for (size_t index = 0;
             index < sizeof(driver_syscalls) / sizeof(driver_syscalls[0]);
             ++index) profile_allow(profile, driver_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_AUDIO_SERVICE) {
        /* The PCM policy service owns no MMIO, IRQ or DMA authority. It can
         * communicate only through bounded IPC and supervisor reports. */
        static const uint8_t audio_service_syscalls[] = {
            SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
            SYS_IPC_CREATE, SYS_IPC_SEND, SYS_IPC_RECEIVE, SYS_IPC_CLOSE,
            SYS_IPC_RELEASE, SYS_IPC_SEND_TIMEOUT, SYS_IPC_RECEIVE_TIMEOUT,
            SYS_REIST_REPORT, SYS_SERVICE_CONNECT
        };
        for (size_t index = 0;
             index < sizeof(audio_service_syscalls) /
                         sizeof(audio_service_syscalls[0]);
             ++index) profile_allow(profile, audio_service_syscalls[index]);
        return true;
    }
    if (kind == PROCESS_DOMAIN_COMPOSITOR) {
        /* The trusted session compositor owns display publication and its
         * bounded Surface broker, but receives no device, raw storage,
         * network-policy or administrative authority. */
        static const uint8_t compositor_syscalls[] = {
            0U, 1U, SYS_EXIT, SYS_GET_DATE, SYS_GET_TIME,
            SYS_OPEN, SYS_READ, SYS_CLOSE, SYS_STAT, SYS_READDIR_BATCH,
            SYS_CREATE, SYS_WRITE, SYS_UNLINK, SYS_SPAWN, SYS_WAIT, SYS_KILL,
            SYS_SPAWNV, SYS_MKDIR, SYS_RMDIR, SYS_CLEAR,
            SYS_TERMINAL_WRITE, SYS_GETCHAR_NONBLOCKING, SYS_TERMINAL_INPUT,
            SYS_YIELD, SYS_SLEEP_MS,
            SYS_MONOTONIC_MS, SYS_DISPLAY_INFO, SYS_FILL_RECT, SYS_DRAW_TEXT,
            SYS_RENAME, SYS_FSYNC, SYS_IPC_CREATE, SYS_IPC_CLOSE,
            SYS_IPC_SEND_TIMEOUT,
            SYS_IPC_RECEIVE_TIMEOUT, SYS_IPC_DELEGATE, SYS_IPC_RELEASE,
            SYS_REIST_REPORT, SYS_SERVICE_CONNECT, SYS_DISPLAY_CONTROL,
            SYS_MOUSE_EVENT, SYS_POINTER_UPDATE, SYS_PROCESS_IDENTITY,
            SYS_DRAW_TEXT_CLIPPED,
            SYS_STORAGE_SUBMIT, SYS_STORAGE_COLLECT, SYS_STORAGE_CANCEL,
            SYS_STORAGE_BULK
        };
        for (size_t index = 0;
             index < sizeof(compositor_syscalls) /
                         sizeof(compositor_syscalls[0]);
             ++index) profile_allow(profile, compositor_syscalls[index]);
        return true;
    }
    if (kind != PROCESS_DOMAIN_PROBE) return false;

    static const uint8_t probe_syscalls[] = {
        SYS_EXIT, SYS_GETPID, SYS_YIELD, SYS_SLEEP_MS, SYS_MONOTONIC_MS,
        SYS_MEMORY_STATS, SYS_SCHEDULER_STATS, SYS_RUNTIME_TIMING,
        SYS_IPC_CREATE, SYS_IPC_SEND, SYS_IPC_RECEIVE,
        SYS_IPC_CLOSE,
        SYS_IPC_SEND_TIMEOUT, SYS_IPC_RECEIVE_TIMEOUT, SYS_REIST_REPORT,
        SYS_NETWORK_PROBE, SYS_NETWORK_PROBE_ID, SYS_REIST_ARP_BINDING,
        SYS_REIST_ARP_REPLY, SYS_REIST_ARP_RESOLUTION,
        SYS_REIST_ICMP_ECHO_REPLY, SYS_REIST_DHCP_COMMIT,
        SYS_REIST_UDP_ECHO_REPLY, SYS_REIST_UDP_BIND,
        SYS_REIST_UDP_UNBIND, SYS_REIST_UDP_REPLY, SYS_REIST_DHCP_RENEW,
        SYS_REIST_NETWORK_FRAME, SYS_REIST_UDP_INGRESS,
        SYS_REIST_DHCP_INGRESS, SYS_REIST_DHCP_BOOT_START,
        SYS_REIST_ICMP_INGRESS, SYS_UDP_SOCKET_INGRESS,
        SYS_TCP_SOCKET_INGRESS
    };
    for (size_t index = 0;
         index < sizeof(probe_syscalls) / sizeof(probe_syscalls[0]); ++index) {
        profile_allow(profile, probe_syscalls[index]);
    }
    return true;
}

bool process_syscall_allowed(const Process *process, uint32_t syscall_index) {
    if (process == NULL) return true; /* Explicit trusted kernel path. */
    const process_domain_profile_t *profile = &process->domain_profile;
    if (profile->version != PROCESS_DOMAIN_PROFILE_VERSION ||
        profile->struct_size != sizeof(*profile) ||
        syscall_index >= PROCESS_DOMAIN_SYSCALL_LIMIT ||
        (profile->kind != PROCESS_DOMAIN_COMPATIBILITY &&
         profile->kind != PROCESS_DOMAIN_PROBE &&
         profile->kind != PROCESS_DOMAIN_STORAGE &&
         profile->kind != PROCESS_DOMAIN_ADMIN &&
         profile->kind != PROCESS_DOMAIN_COMPONENT_ADMIN &&
         profile->kind != PROCESS_DOMAIN_DRIVER &&
         profile->kind != PROCESS_DOMAIN_AUDIO_SERVICE &&
         profile->kind != PROCESS_DOMAIN_COMPOSITOR &&
         profile->kind != PROCESS_DOMAIN_SCRIPT &&
         profile->kind != PROCESS_DOMAIN_MAINTENANCE)) return false;
    return (profile->allowed_syscalls[syscall_index / 32U] &
            (1U << (syscall_index % 32U))) != 0U;
}

int process_restrict_script(Process *process) {
    if (process == NULL) return -13;
    process_domain_profile_t reduced;
    if (!initialize_domain_profile(&reduced, PROCESS_DOMAIN_SCRIPT)) return -13;
    uint32_t flags = process_table_lock_irqsave();
    int result = -13;
    if (!process->is_running || process->has_exited || process->terminating ||
        !process->generation ||
        !process_syscall_allowed(process, SYS_PROCESS_RESTRICT) ||
        (process->domain_profile.kind != PROCESS_DOMAIN_COMPATIBILITY &&
         process->domain_profile.kind != PROCESS_DOMAIN_SCRIPT)) goto done;
    for (unsigned i = 0; i < PROCESS_DOMAIN_SYSCALL_WORDS; ++i)
        if (reduced.allowed_syscalls[i] &
            ~process->domain_profile.allowed_syscalls[i]) goto done;
    if (process->heap_bytes > PROCESS_SCRIPT_HEAP_MAX_BYTES) {
        result = -12;
        goto done;
    }
    /* Zero remains lazy: the allocator must still derive the physical-RAM
     * budget. A pre-existing lower budget never grows during attenuation. */
    if (process->heap_budget > PROCESS_SCRIPT_HEAP_MAX_BYTES)
        process->heap_budget = PROCESS_SCRIPT_HEAP_MAX_BYTES;
    process->domain_profile = reduced;
    result = 0;
done:
    process_table_unlock_irqrestore(flags);
    return result;
}

static int allocate_pid_locked(void) {
    for (int attempt = 0; attempt <= MAX_PROGRAMS; ++attempt) {
        int candidate = next_pid;
        next_pid = next_pid == INT_MAX ? 1 : next_pid + 1;

        bool in_use = false;
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if ((process_list[i].is_running || process_list[i].has_exited) &&
                process_list[i].pid == candidate) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            return candidate;
        }
    }
    return -1;
}

static void release_process_slot(Process *process) {
    /* Fence device output before IPC/capability teardown. Reset and
     * reintegration remain a bounded supervisor transaction. */
    device_domain_process_cleanup(process->pid, process->generation);
    ipc_process_cleanup(process->pid, process->generation);
    framebuffer_frame_process_cleanup(process->pid, process->generation);
    process_close_all_files(process);
    net_socket_process_cleanup(process->pid, process->generation);
    tcp_socket_process_cleanup(process->pid, process->generation);
    storage_request_cancel_process(process->pid, process->generation);
    admin_maintenance_process_cleanup(process->pid, process->generation);
    component_control_process_cleanup(process->pid, process->generation);
    uint32_t flags = process_table_lock_irqsave();
    terminal_input_process_cleanup(process->pid, process->generation);
    process->is_running = false;
    process->terminating = false;
    process->uses_shared_program_image = false;
    process->task_id = -1;
    process_table_unlock_irqrestore(flags);
}

static void initialize_standard_descriptors(Process *process) {
    process->files[0] = (process_file_t){
        .kind = PROCESS_DESCRIPTOR_TERMINAL_INPUT,
        .in_use = true,
        .readable = true,
    };
    process->files[1] = (process_file_t){
        .kind = PROCESS_DESCRIPTOR_TERMINAL_OUTPUT,
        .in_use = true,
        .writable = true,
    };
    process->files[2] = (process_file_t){
        .kind = PROCESS_DESCRIPTOR_TERMINAL_OUTPUT,
        .in_use = true,
        .writable = true,
    };
}

/* Reserve list state atomically, but do slow file/heap work with IRQs enabled. */
static int claim_process_slot(const char *name, bool shared_image,
                              int parent_pid, uint32_t parent_generation,
                              bool supervised,
                              process_domain_kind_t domain_kind) {
    uint32_t flags = process_table_lock_irqsave();

    if (shared_image) {
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if (process_list[i].is_running &&
                process_list[i].uses_shared_program_image) {
                process_table_unlock_irqrestore(flags);
                return -2;
            }
        }
    }

    size_t free_slots = 0U;
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (!process_list[i].is_running &&
            !(process_list[i].has_exited && process_list[i].parent_pid > 0) &&
            process_list[i].generation != UINT32_MAX) ++free_slots;
    }
    if (free_slots == 0U ||
        (!supervised && free_slots <= SUPERVISED_PROCESS_RESERVE)) {
        process_table_unlock_irqrestore(flags);
        return -1;
    }

    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (!process_list[i].is_running &&
            !(process_list[i].has_exited && process_list[i].parent_pid > 0)) {
            Process *process = &process_list[i];
            /* Generation wrap would make stale process identities valid
             * again.  Retire the slot instead of reusing generation zero. */
            if (process->generation == UINT32_MAX) continue;
            int pid = allocate_pid_locked();
            if (pid < 0) {
                process_table_unlock_irqrestore(flags);
                return -1;
            }
            uint32_t generation = process->generation + 1U;
            process->pid = pid;
            process->generation = generation;
            process->parent_pid = parent_pid;
            process->parent_generation = parent_generation;
            process->task_id = -1;
            process->exit_status = 0;
            process->has_exited = false;
            process->terminating = false;
            process->uses_shared_program_image = shared_image;
            process->heap_next = USER_HEAP_BASE;
            process->heap_bytes = 0U;
            process->heap_budget = 0U;
            memset(process->user_allocations, 0,
                   sizeof(process->user_allocations));
            memset(process->files, 0, sizeof(process->files));
            initialize_standard_descriptors(process);
            if (!initialize_domain_profile(&process->domain_profile,
                                           domain_kind)) {
                process->is_running = false;
                process_table_unlock_irqrestore(flags);
                return -1;
            }
            memset(process->ipc_capabilities, 0,
                   sizeof(process->ipc_capabilities));
            wait_queue_init(&process->exit_waiters);
            strcpy(process->working_directory, "/");
            process->image_path[0] = '\0';
            strncpy(process->name, name, sizeof(process->name) - 1U);
            process->name[sizeof(process->name) - 1U] = '\0';
            process->is_running = true;
            process_table_unlock_irqrestore(flags);
            return i;
        }
    }

    process_table_unlock_irqrestore(flags);
    return -1;
}

int create_process_for_file(const char *filename) {
    const char *arguments[] = {filename};
    return create_process_for_file_args(filename, 1, arguments, "/");
}

static int build_user_arguments(page_directory_t *page_directory, int argc,
                                const char *const *argv,
                                uint32_t *user_stack) {
    if (argc < 0 || (unsigned)argc > PROCESS_ARGUMENT_INTERNAL_COUNT ||
        (argc != 0 && argv == NULL) || user_stack==NULL) return -22;
    uint32_t addresses[PROCESS_ARGUMENT_INTERNAL_COUNT];
    size_t lengths[PROCESS_ARGUMENT_INTERNAL_COUNT];
    size_t used=((size_t)argc+4U)*sizeof(uint32_t)+3U;
    /* Entire initial frame is admitted before any copyout. Preserve at least
     * half the existing guarded user stack for execution, not argv storage. */
    if(PROCESS_ARGUMENT_TOTAL_BYTES>(USER_STACK_TOP-USER_STACK_BOTTOM)/2U) return -7;
    for(int i=0;i<argc;++i) {
        if(!argv[i]) return -22;
        size_t n=0; while(n<PROCESS_ARGUMENT_STRING_BYTES && argv[i][n]) ++n;
        if(n==PROCESS_ARGUMENT_STRING_BYTES || n+1U>PROCESS_ARGUMENT_TOTAL_BYTES-used) return -7;
        lengths[i]=n+1U; used+=n+1U;
    }
    uint32_t stack = USER_STACK_TOP;

    for (int i = argc - 1; i >= 0; --i) {
        size_t length=lengths[i];
        stack -= (uint32_t)length;
        if (copy_to_user_space(page_directory, stack, argv[i], length) != 0) {
            return -1;
        }
        addresses[i] = stack;
    }

    stack &= ~3U;
    uint32_t zero = 0;
    stack -= sizeof(uint32_t);
    if (copy_to_user_space(page_directory, stack, &zero, sizeof(zero)) != 0) {
        return -1;
    }
    for (int i = argc - 1; i >= 0; --i) {
        stack -= sizeof(uint32_t);
        if (copy_to_user_space(page_directory, stack, &addresses[i],
                               sizeof(addresses[i])) != 0) return -1;
    }
    uint32_t user_argv = stack;
    stack -= sizeof(uint32_t);
    if (copy_to_user_space(page_directory, stack, &user_argv,
                           sizeof(user_argv)) != 0) return -1;
    stack -= sizeof(uint32_t);
    uint32_t user_argc = (uint32_t)argc;
    if (copy_to_user_space(page_directory, stack, &user_argc,
                           sizeof(user_argc)) != 0) return -1;
    stack -= sizeof(uint32_t);
    if (copy_to_user_space(page_directory, stack, &zero, sizeof(zero)) != 0) {
        return -1;
    }
    *user_stack = stack;
    return 0;
}

static int create_process_for_file_args_owned(const char *filename, int argc,
                                               const char *const *argv,
                                               const char *working_directory,
                                               Process *parent,
                                               bool supervised,
                                               bool prepared,
                                               process_domain_kind_t domain_kind,
                                               uint32_t cpu_affinity_mask) {
    if (filename == NULL || *filename == '\0') {
        return -22;
    }
    if (strlen(filename) >= PROCESS_PATH_MAX) return -36;

    /* Reclaim stacks and address spaces before this spawn needs new frames.
     * Exit status remains in the Process zombie until its parent waits. */
    (void)scheduler_reap_finished_tasks();

    const char *display_name = filename;
    for (const char *cursor = filename; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') display_name = cursor + 1;
    }
    int parent_pid = parent != NULL ? parent->pid : 0;
    uint32_t parent_generation = parent != NULL ? parent->generation : 0U;
    int slot = claim_process_slot(display_name, false, parent_pid,
                                  parent_generation, supervised, domain_kind);
    if (slot < 0) {
        printf("Error: Maximum number of running programs reached.\n");
        return -11;
    }

    Process *process = &process_list[slot];
    if (working_directory == NULL || working_directory[0] != '/' ||
        strlen(working_directory) >= sizeof(process->working_directory)) {
        release_process_slot(process);
        printf("PROGRAM_SPAWN_FAIL stage=working-directory path=%s\n", filename);
        return -22;
    }
    strcpy(process->working_directory, working_directory);
    strcpy(process->image_path, filename);
    int pid = process->pid;
    uint8_t *program_image = NULL;
    int loaded_size = load_program_file(filename, &program_image);
    if (loaded_size < 0) {
        printf("Unable to load valid program '%s'\n", filename);
        release_process_slot(process);
        return -2;
    }

    program_header_t* header = (program_header_t*)program_image;
    uint32_t memory_image_size = (uint32_t)sizeof(*header) +
                                 header->program_size;
    uint32_t stored_image_size = header->relocation_offset;
    uint32_t entry_point = header->entry_point;

    uint32_t program_frames =
        (memory_image_size + PAGE_SIZE - 1U) / PAGE_SIZE;
    uint32_t stack_frames = (USER_STACK_TOP - USER_STACK_BOTTOM) / PAGE_SIZE;
    uint64_t required_frames = (uint64_t)program_frames + stack_frames + 16U;
    memory_stats_t memory_stats;
    memory_get_stats(&memory_stats);
    uint64_t reserve_frames = supervised
        ? 0U : SUPERVISED_RESTART_FRAME_RESERVE;
    if (required_frames + reserve_frames >
        memory_stats.free_frame_bytes / PAGE_SIZE) {
        release_admission_image(&program_image);
        release_process_slot(process);
        printf("PROGRAM_SPAWN_FAIL stage=frame-admission path=%s\n", filename);
        return -12;
    }

    page_directory_t *page_directory = create_page_directory();
    if (page_directory == NULL) {
        k_free(program_image);
        release_process_slot(process);
        printf("PROGRAM_SPAWN_FAIL stage=page-directory path=%s\n", filename);
        return -12;
    }

    uint32_t mapped_size = (memory_image_size + PAGE_SIZE - 1U) &
                           ~(PAGE_SIZE - 1U);
    for (uint32_t offset = 0; offset < mapped_size; offset += PAGE_SIZE) {
        uint32_t frame = (uint32_t)allocate_frame();
        if (frame == 0 || map_page(page_directory,
                                  USER_PROGRAM_ADDRESS + offset, frame,
                                  PAGE_USER | PAGE_RW) != 0) {
            if (frame != 0) free_frame(frame);
            free_page_directory(page_directory);
            k_free(program_image);
            release_process_slot(process);
            printf("PROGRAM_SPAWN_FAIL stage=program-map path=%s\n", filename);
            return -12;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
        uint32_t amount = 0;
        if (offset < stored_image_size) {
            amount = stored_image_size - offset;
            if (amount > PAGE_SIZE) amount = PAGE_SIZE;
            memcpy((void*)(uintptr_t)frame,
                   program_image + offset,
                   amount);
        }
    }
    /* Fresh page directories leave both adjacent guard pages non-present.
     * Only the bounded stack interval is mapped user-writable. */
    for (uint32_t address = USER_STACK_BOTTOM;
         address < USER_STACK_TOP; address += PAGE_SIZE) {
        uint32_t frame = (uint32_t)allocate_frame();
        if (frame == 0 || map_page(page_directory, address, frame,
                                  PAGE_USER | PAGE_RW) != 0) {
            if (frame != 0) free_frame(frame);
            free_page_directory(page_directory);
            k_free(program_image);
            release_process_slot(process);
            printf("PROGRAM_SPAWN_FAIL stage=stack-map path=%s\n", filename);
            return -12;
        }
        memset((void*)(uintptr_t)frame, 0, PAGE_SIZE);
    }
    k_free(program_image);

    uint32_t* kernel_stack = scheduler_allocate_kernel_stack();
    if (kernel_stack == NULL) {
        free_page_directory(page_directory);
        release_process_slot(process);
        printf("PROGRAM_SPAWN_FAIL stage=kernel-stack path=%s\n", filename);
        return -12;
    }

    uint32_t user_stack;
    if (build_user_arguments(page_directory, argc, argv, &user_stack) != 0) {
        scheduler_free_kernel_stack(kernel_stack);
        free_page_directory(page_directory);
        release_process_slot(process);
        printf("PROGRAM_SPAWN_FAIL stage=arguments path=%s argc=%d\n",
               filename, argc);
        return -14;
    }

    int task_id;
    if (!supervised) {
        task_id = create_user_task(entry_point + USER_PROGRAM_ADDRESS,
            user_stack, kernel_stack, page_directory, process);
    } else if (prepared) {
        task_id = create_prepared_supervised_user_task(
            entry_point + USER_PROGRAM_ADDRESS, user_stack,
            kernel_stack, page_directory, process);
    } else {
        task_id = create_affined_supervised_user_task(
            entry_point + USER_PROGRAM_ADDRESS, user_stack,
            kernel_stack, page_directory, process, cpu_affinity_mask);
    }
    if (task_id < 0) {
        scheduler_free_kernel_stack(kernel_stack);
        free_page_directory(page_directory);
        release_process_slot(process);
        printf("PROGRAM_SPAWN_FAIL stage=task-capacity path=%s\n", filename);
        return -16;
    }
    return pid;
}

int create_process_for_file_args(const char *filename, int argc,
                                 const char *const *argv,
                                 const char *working_directory) {
    return create_process_for_file_args_owned(filename, argc, argv,
        working_directory, NULL, false, false, PROCESS_DOMAIN_COMPATIBILITY,
        TASK_CPU_MASK_BSP);
}

int create_process(void* entry_point) {
    if (entry_point == NULL) {
        return -1;
    }

    (void)scheduler_reap_finished_tasks();

    int slot = claim_process_slot("Unknown", false, 0, 0U, true,
                                  PROCESS_DOMAIN_COMPATIBILITY);
    if (slot < 0) {
        printf("Error: Maximum number of running programs reached.\n");
        return -1;
    }
    Process *process = &process_list[slot];
    int pid = process->pid;

    uint32_t* stack = scheduler_allocate_kernel_stack();
    if (stack == NULL) {
        printf("Error: Failed to allocate stack for process\n");
        release_process_slot(process);
        return -1;
    }

    int task_id = create_task((void (*)())entry_point, stack, process);
    if (task_id < 0) {
        scheduler_free_kernel_stack(stack);
        release_process_slot(process);
        return -1;
    }
    return pid;
}

static int append_path_components(char *output, size_t capacity,
                                  const char *path) {
    size_t output_length = strlen(output);
    const char *cursor = path;
    while (*cursor != '\0') {
        while (*cursor == '/' || *cursor == '\\') ++cursor;
        if (*cursor == '\0') break;
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') ++cursor;
        size_t length = (size_t)(cursor - start);
        if (length == 1 && start[0] == '.') continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (output_length > 1) {
                while (output_length > 1 && output[output_length - 1] != '/') {
                    --output_length;
                }
                if (output_length > 1) --output_length;
                output[output_length] = '\0';
            }
            continue;
        }
        if (output_length > 1) {
            if (output_length + 1 >= capacity) return -1;
            output[output_length++] = '/';
        }
        if (length == 0 || length >= capacity - output_length) return -1;
        memcpy(output + output_length, start, length);
        output_length += length;
        output[output_length] = '\0';
    }
    return 0;
}

static const char* process_drive_mount(char letter) {
    if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
    for (int index = 0; index < drive_count; ++index) {
        drive_t* drive = &detected_drives[index];
        if (drive->mount_point[0] == '\0') continue;
        if (strcmp(drive->mount_point, "/") == 0) {
            if (letter == 'C') return drive->mount_point;
            continue;
        }
        if (strlen(drive->name) != 4U || drive->name[3] < '0' ||
            drive->name[3] > '9') continue;
        char mapped = 0;
        if (drive->type == DRIVE_TYPE_FDD) {
            mapped = (char)('A' + drive->name[3] - '0');
        } else if (drive->type == DRIVE_TYPE_ATA ||
                   drive->type == DRIVE_TYPE_AHCI) {
            mapped = (char)('C' + drive->name[3] - '0');
        }
        if (mapped == letter) return drive->mount_point;
    }
    return NULL;
}

int process_resolve_path(const Process *process, const char *path,
                         char resolved[PROCESS_PATH_MAX]) {
    if (process == NULL || path == NULL || path[0] == '\0') return -1;
    strcpy(resolved, "/");
    if (path[1] == ':') {
        const char* mount = process_drive_mount(path[0]);
        if (mount == NULL || strlen(mount) >= PROCESS_PATH_MAX) return -1;
        strcpy(resolved, mount);
        return append_path_components(resolved, PROCESS_PATH_MAX, path + 2);
    }
    if (path[0] != '/' &&
        append_path_components(resolved, PROCESS_PATH_MAX,
                               process->working_directory) != 0) return -1;
    return append_path_components(resolved, PROCESS_PATH_MAX, path);
}

int process_get_working_directory(const Process *process, char *buffer,
                                  size_t size) {
    if (process == NULL || buffer == NULL) return -1;
    size_t length = strlen(process->working_directory) + 1U;
    if (size < length) return -1;
    memcpy(buffer, process->working_directory, length);
    return 0;
}

int process_set_working_directory(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;

    vfs_dir_entry_t entry;
    if (vfs_stat(resolved, &entry) != VFS_OK || entry.type != VFS_DIRECTORY) {
        return -1;
    }
    strcpy(process->working_directory, resolved);
    return 0;
}

int process_file_open(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;

    int slot = -1;
    for (int i = PROCESS_FD_BASE; i < MAX_PROCESS_DESCRIPTORS; ++i) {
        if (!process->files[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    vfs_node_t *node = NULL;
    if (vfs_open(resolved, &node) != VFS_OK || node == NULL) return -1;
    if (node->type != VFS_FILE) {
        (void)vfs_close(node);
        return -1;
    }

    process->files[slot].node = node;
    process->files[slot].offset = 0;
    process->files[slot].kind = PROCESS_DESCRIPTOR_FILE;
    process->files[slot].in_use = true;
    process->files[slot].readable = true;
    process->files[slot].writable = false;
    process->files[slot].append = false;
    return slot;
}

static int process_vfs_errno(int result) {
    switch (result) {
        case VFS_ERR_NOT_FOUND: return -REIST_ENOENT;
        case VFS_ERR_NO_MEMORY: return -REIST_ENOMEM;
        case VFS_ERR_INVALID: return -REIST_EINVAL;
        case VFS_ERR_IO: return -REIST_EIO;
        case VFS_ERR_EXISTS: return -REIST_EEXIST;
        case VFS_ERR_NOT_DIR: return -REIST_ENOTDIR;
        case VFS_ERR_IS_DIR: return -REIST_EISDIR;
        case VFS_ERR_NO_SPACE: return -REIST_ENOSPC;
        case VFS_ERR_READ_ONLY: return -REIST_EROFS;
        case VFS_ERR_UNSUPPORTED: return -REIST_ENOTSUP;
        case VFS_ERR_BUSY: return -REIST_EBUSY;
        default: return -REIST_EIO;
    }
}

int process_file_open_flags(Process *process, const char *path,
                            uint32_t flags) {
    uint32_t access_mode = flags & PROCESS_OPEN_ACCMODE;
    if ((flags & ~PROCESS_OPEN_ALLOWED_FLAGS) != 0U ||
        access_mode == PROCESS_OPEN_ACCMODE ||
        ((flags & PROCESS_OPEN_APPEND) != 0U &&
         access_mode == PROCESS_OPEN_RDONLY) ||
        ((flags & PROCESS_OPEN_TRUNC) != 0U &&
         access_mode == PROCESS_OPEN_RDONLY)) return -REIST_EINVAL;
    if (process == NULL || path == NULL) return -REIST_EINVAL;

    int slot = -1;
    for (int i = PROCESS_FD_BASE; i < MAX_PROCESS_DESCRIPTORS; ++i) {
        if (!process->files[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -REIST_EMFILE;

    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0)
        return -REIST_EINVAL;

    bool created = false;
    vfs_node_t *node = NULL;
    int result = vfs_open(resolved, &node);
    if (result == VFS_ERR_NOT_FOUND && (flags & PROCESS_OPEN_CREAT) != 0U) {
        result = vfs_create(resolved);
        if (result != VFS_OK) return process_vfs_errno(result);
        created = true;
        result = vfs_open(resolved, &node);
    }
    if (result != VFS_OK || node == NULL) {
        if (node != NULL) (void)vfs_close(node);
        if (created) (void)vfs_delete(resolved);
        return result == VFS_OK ? -REIST_EIO : process_vfs_errno(result);
    }
    if (node->type != VFS_FILE) {
        (void)vfs_close(node);
        if (created) (void)vfs_delete(resolved);
        return -REIST_EISDIR;
    }
    if ((flags & PROCESS_OPEN_TRUNC) != 0U) {
        result = vfs_truncate(node, 0U);
        if (result != VFS_OK) {
            (void)vfs_close(node);
            if (created) (void)vfs_delete(resolved);
            return process_vfs_errno(result);
        }
    }

    bool readable = access_mode != PROCESS_OPEN_WRONLY;
    bool writable = access_mode != PROCESS_OPEN_RDONLY;
    process->files[slot] = (process_file_t){
        .node = node,
        .offset = 0U,
        .kind = PROCESS_DESCRIPTOR_FILE,
        .in_use = true,
        .readable = readable,
        .writable = writable,
        .append = (flags & PROCESS_OPEN_APPEND) != 0U,
    };
    return slot;
}

int process_file_read(Process *process, int descriptor, void *buffer,
                      size_t size) {
    int slot = descriptor;
    if (process == NULL || buffer == NULL || slot < PROCESS_FD_BASE ||
        slot >= MAX_PROCESS_DESCRIPTORS || !process->files[slot].in_use ||
        process->files[slot].kind != PROCESS_DESCRIPTOR_FILE ||
        !process->files[slot].readable ||
        size > UINT32_MAX) {
        return -1;
    }
    if (size == 0) return 0;

    process_file_t *file = &process->files[slot];
    int result = vfs_read(file->node, file->offset, (uint32_t)size, buffer);
    if (result > 0) file->offset += (uint32_t)result;
    return result;
}

int process_file_create(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;

    int slot = -1;
    for (int i = PROCESS_FD_BASE; i < MAX_PROCESS_DESCRIPTORS; ++i) {
        if (!process->files[i].in_use) { slot = i; break; }
    }
    if (slot < 0 || vfs_create(resolved) != VFS_OK) return -1;

    vfs_node_t *node = NULL;
    if (vfs_open(resolved, &node) != VFS_OK || node == NULL ||
        node->type != VFS_FILE) {
        if (node != NULL) (void)vfs_close(node);
        (void)vfs_delete(resolved);
        return -1;
    }
    process->files[slot].node = node;
    process->files[slot].offset = 0;
    process->files[slot].kind = PROCESS_DESCRIPTOR_FILE;
    process->files[slot].in_use = true;
    process->files[slot].readable = true;
    process->files[slot].writable = true;
    process->files[slot].append = false;
    return slot;
}

uint32_t process_file_write_chunk_capacity(Process *process,
                                           int descriptor) {
    int slot = descriptor;
    if (process == NULL || slot < PROCESS_FD_BASE ||
        slot >= MAX_PROCESS_DESCRIPTORS || !process->files[slot].in_use ||
        process->files[slot].kind != PROCESS_DESCRIPTOR_FILE ||
        !process->files[slot].writable || process->files[slot].node == NULL)
        return VFS_DEFAULT_WRITE_CHUNK_CAPACITY;
    process_file_t *file = &process->files[slot];
    uint32_t offset = file->append ? file->node->size : file->offset;
    return vfs_write_chunk_capacity(file->node, offset);
}

int process_file_write(Process *process, int descriptor, const void *buffer,
                       size_t size) {
    int slot = descriptor;
    if (process == NULL || buffer == NULL || slot < PROCESS_FD_BASE ||
        slot >= MAX_PROCESS_DESCRIPTORS || !process->files[slot].in_use ||
        process->files[slot].kind != PROCESS_DESCRIPTOR_FILE ||
        !process->files[slot].writable || size > UINT32_MAX) return -1;
    if (size == 0) return 0;
    process_file_t *file = &process->files[slot];
    uint32_t write_offset = file->offset;
    if (file->append) write_offset = file->node->size;
    int result = vfs_write(file->node, write_offset, (uint32_t)size, buffer);
    if (result > 0) file->offset = write_offset + (uint32_t)result;
    return result;
}

int process_file_truncate(Process *process, int descriptor, uint32_t size) {
    if (process == NULL || descriptor < 0 ||
        descriptor >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[descriptor].in_use) return -REIST_EBADF;
    process_file_t *file = &process->files[descriptor];
    if (file->kind != PROCESS_DESCRIPTOR_FILE) return -REIST_EINVAL;
    if (!file->writable) return -REIST_EBADF;
    int result = vfs_truncate(file->node, size);
    return result == VFS_OK ? 0 : process_vfs_errno(result);
}

int process_file_seek(Process *process, int descriptor, int32_t offset,
                      uint32_t whence) {
    if (process == NULL || descriptor < 0 ||
        descriptor >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[descriptor].in_use) return -REIST_EBADF;
    process_file_t *file = &process->files[descriptor];
    if (file->kind != PROCESS_DESCRIPTOR_FILE) return -REIST_ESPIPE;
    if (whence > PROCESS_SEEK_END) return -REIST_EINVAL;

    int64_t base = 0;
    if (whence == PROCESS_SEEK_CUR) {
        base = (int64_t)file->offset;
    } else if (whence == PROCESS_SEEK_END) {
        vfs_dir_entry_t entry;
        int result = vfs_fstat(file->node, &entry);
        if (result != VFS_OK) return process_vfs_errno(result);
        base = (int64_t)entry.size;
    }
    int64_t candidate = base + (int64_t)offset;
    if (candidate < 0) return -REIST_EINVAL;
    if (candidate > INT32_MAX) return -REIST_EOVERFLOW;
    file->offset = (uint32_t)candidate;
    return (int)candidate;
}

int process_file_fstat(Process *process, int descriptor,
                       vfs_dir_entry_t *entry) {
    if (process == NULL || entry == NULL || descriptor < 0 ||
        descriptor >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[descriptor].in_use) return -REIST_EBADF;
    process_file_t *file = &process->files[descriptor];
    if (file->kind != PROCESS_DESCRIPTOR_FILE) return -REIST_ESPIPE;
    int result = vfs_fstat(file->node, entry);
    return result == VFS_OK ? 0 : process_vfs_errno(result);
}

int process_file_sync(Process *process, int descriptor) {
    int slot = descriptor;
    if (process == NULL || slot < PROCESS_FD_BASE ||
        slot >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[slot].in_use || !process->files[slot].writable) {
        return -1;
    }
    if (process->files[slot].kind != PROCESS_DESCRIPTOR_FILE) return -1;
    return vfs_sync(process->files[slot].node) == VFS_OK ? 0 : -1;
}

int process_file_unlink(Process *process, const char *path) {
    if (process == NULL || path == NULL) return -1;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(process, path, resolved) != 0) return -1;
    return vfs_delete(resolved) == VFS_OK ? 0 : -1;
}

int process_file_close(Process *process, int descriptor) {
    int slot = descriptor;
    if (process == NULL || slot < 0 || slot >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[slot].in_use) {
        return -1;
    }

    process_file_t *file = &process->files[slot];
    int result = -1;
    if (file->kind == PROCESS_DESCRIPTOR_TERMINAL_INPUT ||
        file->kind == PROCESS_DESCRIPTOR_TERMINAL_OUTPUT)
        result = 0;
    else if (file->kind == PROCESS_DESCRIPTOR_FILE)
        result = vfs_close(file->node) == VFS_OK ? 0 : -1;
    else if (file->kind == PROCESS_DESCRIPTOR_UDP_SOCKET)
        result = net_socket_close(process->pid, process->generation,
                                  file->resource_handle);
    else if (file->kind == PROCESS_DESCRIPTOR_TCP_SOCKET)
        result = tcp_socket_close(process->pid, process->generation,
                                  file->resource_handle, 0U);
    if (result != 0) return -1;
    memset(file, 0, sizeof(*file));
    return 0;
}

int process_descriptor_install(Process *process, uint8_t kind,
                               uint32_t resource_handle) {
    if (process == NULL || resource_handle == 0U ||
        (kind != PROCESS_DESCRIPTOR_UDP_SOCKET &&
         kind != PROCESS_DESCRIPTOR_TCP_SOCKET)) return -22;
    for (int slot = PROCESS_FD_BASE; slot < MAX_PROCESS_DESCRIPTORS; ++slot) {
        if (process->files[slot].in_use) continue;
        process->files[slot] = (process_file_t){
            .resource_handle = resource_handle, .kind = kind, .in_use = true,
        };
        return slot;
    }
    return -24;
}

int process_descriptor_resolve(const Process *process, int descriptor,
                               uint8_t kind, uint32_t *resource_handle_out) {
    int slot = descriptor;
    if (process == NULL || resource_handle_out == NULL ||
        slot < PROCESS_FD_BASE || slot >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[slot].in_use ||
        process->files[slot].kind != kind ||
        process->files[slot].resource_handle == 0U) return -9;
    *resource_handle_out = process->files[slot].resource_handle; return 0;
}

int process_descriptor_validate_access(const Process *process, int descriptor,
                                       bool write_access, uint8_t *kind_out) {
    if (process == NULL || kind_out == NULL || descriptor < 0 ||
        descriptor >= MAX_PROCESS_DESCRIPTORS ||
        !process->files[descriptor].in_use) return -9;
    const process_file_t *entry = &process->files[descriptor];
    if ((write_access && !entry->writable) ||
        (!write_access && !entry->readable)) return -9;
    *kind_out = entry->kind;
    return 0;
}

int process_descriptor_release(Process *process, int descriptor, uint8_t kind) {
    uint32_t handle = 0U;
    if (process_descriptor_resolve(process, descriptor, kind, &handle) != 0)
        return -9;
    (void)handle;
    memset(&process->files[descriptor], 0, sizeof(process_file_t));
    return 0;
}

void process_close_all_files(Process *process) {
    if (process == NULL) return;
    for (int i = 0; i < MAX_PROCESS_DESCRIPTORS; ++i) {
        if (process->files[i].in_use) {
            (void)process_file_close(process, i);
        }
    }
    /* Shared by normal exit, user faults, remote termination and failed
     * spawn. All callers have released lifecycle spinlocks before teardown.
     * A contended guard retains deny-only pins for the bounded poll sweep. */
    vfs_file_object_guard_process_cleanup(process->pid, process->generation);
}

int process_revoke_files_for_resource(uint32_t resource,
                                      uint32_t* revoked_out) {
    if (revoked_out == NULL || resource >= (uint32_t)drive_count) return -22;
    drive_t *drive = &detected_drives[resource];
    uint32_t revoked = 0U;
    int result = 0;
    scheduler_preempt_disable();
    for (int process_index = 0; process_index < MAX_PROGRAMS;
         ++process_index) {
        Process *process = &process_list[process_index];
        if (!process->is_running && !process->has_exited) continue;
        for (int file_index = PROCESS_FD_BASE;
             file_index < MAX_PROCESS_DESCRIPTORS;
             ++file_index) {
            process_file_t *file = &process->files[file_index];
            if (!file->in_use || file->kind != PROCESS_DESCRIPTOR_FILE ||
                file->node == NULL ||
                file->node->fs == NULL || file->node->fs->drive != drive)
                continue;
            if (vfs_close(file->node) != VFS_OK) {
                result = -5;
                continue;
            }
            memset(file, 0, sizeof(*file));
            if (revoked != UINT32_MAX) ++revoked;
        }
    }
    scheduler_preempt_enable();
    *revoked_out = revoked;
    return result;
}

void wait_for_process(int pid) {
    if (pid < 0) return;

    for (;;) {
        bool running = false;
        uint32_t flags = process_table_lock_irqsave();
        for (int i = 0; i < MAX_PROGRAMS; ++i) {
            if (process_list[i].pid == pid && process_list[i].is_running) {
                running = true;
                break;
            }
        }
        process_table_unlock_irqrestore(flags);
        if (!running) return;

        /* The APIC timer schedules the userspace task. HLT avoids burning the
         * shell's kernel context while it waits for the foreground process. */
        __asm__ __volatile__("hlt");
    }
}

int process_spawn(Process *parent, const char *path) {
    if (parent == NULL || path == NULL || *path == '\0') return -1;
    const char *arguments[] = {path};
    return process_spawn_args(parent, path, 1, arguments);
}

int process_spawn_args(Process *parent, const char *path, int argc,
                       const char *const *argv) {
    if (parent == NULL || path == NULL || *path == '\0' || argc < 1 ||
        argc > 32 || argv == NULL) return -22;
    char resolved[PROCESS_PATH_MAX];
    if (process_resolve_path(parent, path, resolved) != 0) {
        printf("PROGRAM_SPAWN_FAIL stage=resolve pid=%d path=%s\n",
               parent->pid, path);
        return -36;
    }
    canonicalize_program_path(resolved);
    process_domain_kind_t domain_kind = strcmp(resolved, "/sbin/svcctl.prg") == 0
        ? PROCESS_DOMAIN_COMPONENT_ADMIN :
        strcmp(resolved, "/sbin/chkdsk.prg") == 0
            ? PROCESS_DOMAIN_MAINTENANCE :
        strcmp(resolved, "/sbin/devctl.prg") == 0 ||
        strcmp(resolved, "/sbin/fdisk.prg") == 0 ||
        strcmp(resolved, "/sbin/format.prg") == 0 ||
        strcmp(resolved, "/sbin/mount.prg") == 0 ||
        strcmp(resolved, "/sbin/umount.prg") == 0
            ? PROCESS_DOMAIN_ADMIN : PROCESS_DOMAIN_COMPATIBILITY;
    return create_process_for_file_args_owned(
        resolved, argc, argv, parent->working_directory, parent, false,
        false, domain_kind, TASK_CPU_MASK_BSP);
}

int process_spawn_supervised(const char *path, int argc,
                             const char *const *argv,
                             process_domain_kind_t domain_kind) {
    if (path == NULL || *path == '\0' || argc < 1 || argc > 32 ||
        argv == NULL) return -1;
    return create_process_for_file_args_owned(path, argc, argv, "/", NULL,
                                               true, false, domain_kind,
                                               TASK_CPU_MASK_BSP);
}

int process_spawn_supervised_affined(const char *path, int argc,
                                     const char *const *argv,
                                     process_domain_kind_t domain_kind,
                                     uint32_t cpu_affinity_mask) {
    if (path == NULL || *path == '\0' || argc < 1 || argc > 32 ||
        argv == NULL || cpu_affinity_mask == 0U) return -1;
    return create_process_for_file_args_owned(path, argc, argv, "/", NULL,
                                               true, false, domain_kind,
                                               cpu_affinity_mask);
}

int process_spawn_supervised_prepared(const char *path, int argc,
                                      const char *const *argv,
                                      process_domain_kind_t domain_kind) {
    if (path == NULL || *path == '\0' || argc < 1 || argc > 32 ||
        argv == NULL) return -1;
    return create_process_for_file_args_owned(path, argc, argv, "/", NULL,
                                               true, true, domain_kind,
                                               TASK_CPU_MASK_BSP);
}

int process_start_prepared_supervised(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return -22;
    uint32_t flags = process_table_lock_irqsave();
    int result = -3;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        Process *process = &process_list[index];
        if (!process->is_running || process->has_exited ||
            process->terminating || process->pid != pid ||
            process->generation != generation) continue;
        result = scheduler_start_prepared_user_task_locked(
            process->task_id, process, generation);
        break;
    }
    process_table_unlock_irqrestore(flags);
    return result;
}

int process_set_supervised_affinity(int pid, uint32_t generation,
                                    uint32_t cpu_affinity_mask) {
    if (pid <= 0 || generation == 0U || cpu_affinity_mask == 0U) return -22;
    uint32_t flags = process_table_lock_irqsave();
    int result = -3;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        Process *process = &process_list[index];
        if (!process->is_running || process->has_exited ||
            process->terminating || process->pid != pid ||
            process->generation != generation) continue;
        result = scheduler_set_task_affinity_locked(
            process->task_id, process, generation, cpu_affinity_mask);
        break;
    }
    process_table_unlock_irqrestore(flags);
    return result;
}

int process_ipc_delegate(Process *source, ipc_handle_t handle,
                         int target_pid, uint32_t rights) {
    if (source == NULL || target_pid <= 0 || target_pid == source->pid) {
        return -22;
    }

    /* Process -> IPC is the fixed lifecycle/capability lock order. */
    uint32_t flags = process_table_lock_irqsave();
    Process *target = NULL;
    uint32_t target_generation = 0U;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        if (process_list[index].is_running &&
            process_list[index].pid == target_pid) {
            target = &process_list[index];
            target_generation = process_list[index].generation;
            break;
        }
    }
    int result = target != NULL && target->generation == target_generation
        ? ipc_delegate(source, handle, target, rights) : -3;
    process_table_unlock_irqrestore(flags);
    return result;
}

int process_ipc_delegate_identity(int source_pid, uint32_t source_generation,
                                  ipc_handle_t handle, Process *target,
                                  uint32_t rights) {
    if (source_pid <= 0 || source_generation == 0U || target == NULL)
        return -22;
    uint32_t flags = process_table_lock_irqsave();
    Process *source = NULL;
    for (size_t index = 0; index < MAX_PROGRAMS; ++index) {
        Process *candidate = &process_list[index];
        if (candidate->is_running && candidate->pid == source_pid &&
            candidate->generation == source_generation) {
            source = candidate;
            break;
        }
    }
    int result = source != NULL
        ? ipc_delegate(source, handle, target, rights) : -9;
    process_table_unlock_irqrestore(flags);
    return result;
}

int process_wait_status_locked(Process *parent, int pid, int *status,
                               wait_queue_t **wait_queue) {
    KASSERT_IRQ_DISABLED();
    KASSERT(process_table_lock_is_owned());
    if (parent == NULL || status == NULL || pid <= 0) return -1;
    if (wait_queue != NULL) *wait_queue = NULL;
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        Process *child = &process_list[i];
        if (child->pid != pid || child->parent_pid != parent->pid) continue;
        if (child->is_running) {
            if (wait_queue != NULL) *wait_queue = &child->exit_waiters;
            return 0;
        }
        if (!child->has_exited) {
            return -1;
        }
        *status = child->exit_status;
        if (child->task_id >= 0 &&
            scheduler_reap_finished_task_locked(child->task_id, child) == 0) {
            child->task_id = -1;
        }
        child->has_exited = false;
        child->parent_pid = 0;
        return 1;
    }
    return -1;
}

int process_wait_status(Process *parent, int pid, int *status) {
    uint32_t flags = process_table_lock_irqsave();
    int result = process_wait_status_locked(parent, pid, status, NULL);
    process_table_unlock_irqrestore(flags);
    if (result > 0) (void)scheduler_reap_finished_tasks();
    return result;
}

void process_orphan_children(int parent_pid) {
    if (parent_pid <= 0) return;
    uint32_t flags = process_table_lock_irqsave();
    for (int i = 0; i < MAX_PROGRAMS; ++i) {
        if (process_list[i].parent_pid == parent_pid) {
            process_list[i].parent_pid = 0;
            if (!process_list[i].is_running) {
                if (process_list[i].task_id >= 0 &&
                    scheduler_reap_finished_task_locked(
                        process_list[i].task_id, &process_list[i]) == 0) {
                    process_list[i].task_id = -1;
                }
                process_list[i].has_exited = false;
            }
        }
    }
    process_table_unlock_irqrestore(flags);
}

int process_get_info_for(const Process *viewer, uint32_t index,
                         process_info_t *info) {
    if (info == NULL) return -1;
    uint32_t flags = process_table_lock_irqsave();
    uint32_t visible = 0;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        Process* process = &process_list[i];
        if (!process->is_running && !process->has_exited) continue;
        if (viewer && viewer->domain_profile.kind == PROCESS_DOMAIN_SCRIPT &&
            process != viewer) continue;
        if (visible++ != index) continue;

        memset(info, 0, sizeof(*info));
        info->pid = process->pid;
        info->parent_pid = process->parent_pid;
        info->exit_status = process->exit_status;
        strncpy(info->name, process->name, sizeof(info->name) - 1U);
        if (process->has_exited) {
            info->state = PROCESS_STATE_ZOMBIE;
        } else {
            int task_state;
            if (scheduler_task_state_snapshot(
                    process->task_id, process, process->generation,
                    &task_state) != 0) {
                info->state = PROCESS_STATE_READY;
                process_table_unlock_irqrestore(flags);
                return 1;
            }
            if (task_state == TASK_RUNNING) info->state = PROCESS_STATE_RUNNING;
            else if (task_state == TASK_SLEEPING) info->state = PROCESS_STATE_SLEEPING;
            else if (task_state == TASK_WAITING) info->state = PROCESS_STATE_WAITING;
            else info->state = PROCESS_STATE_READY;
        }
        process_table_unlock_irqrestore(flags);
        return 1;
    }
    process_table_unlock_irqrestore(flags);
    return 0;
}

int process_get_info(uint32_t index, process_info_t *info) {
    return process_get_info_for(NULL, index, info);
}

static int process_terminate_generation(int pid, uint32_t generation) {
    if (pid <= 0) return -1;
    KASSERT_NOT_IRQ();
    scheduler_preempt_disable();
    uint32_t flags = process_table_lock_irqsave();
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (process_list[i].is_running && !process_list[i].terminating &&
            process_list[i].pid == pid &&
            (generation == 0U || process_list[i].generation == generation)) {
            int task_id = process_list[i].task_id;
            int task_state;
            if (scheduler_task_state_snapshot(
                    task_id, &process_list[i], process_list[i].generation,
                    &task_state) != 0) {
                process_table_unlock_irqrestore(flags);
                scheduler_preempt_enable();
                return -1;
            }
            bool self = task_id == scheduler_current_task_id();
            if (!self) {
                /* Reserve under Process -> Task before publishing terminating:
                 * another CPU must not claim the target in between. A running
                 * owner must first quiesce through its existing bounded fence. */
                if (!scheduler_reserve_termination_locked(
                        task_id, &process_list[i], process_list[i].generation)) {
                    process_table_unlock_irqrestore(flags);
                    scheduler_preempt_enable();
                    return -1;
                }
                terminal_input_process_cleanup(pid, process_list[i].generation);
                process_list[i].terminating = true;
            }
            process_table_unlock_irqrestore(flags);
            if (self) {
                scheduler_preempt_enable();
                task_exit_status(143);
            }
            /* The terminating state pins PID/slot/generation while the
             * scheduler closes resources with IRQs enabled. */
            scheduler_terminate_task(task_id);
            scheduler_preempt_enable();
            return 0;
        }
    }
    process_table_unlock_irqrestore(flags);
    scheduler_preempt_enable();
    return -1;
}

int process_terminate(int pid) {
    return process_terminate_generation(pid, 0U);
}

int process_terminate_identity(int pid, uint32_t generation) {
    if (generation == 0U) return -1;
    return process_terminate_generation(pid, generation);
}

int process_terminate_authorized(Process *requester, int pid) {
    if (requester == NULL || pid <= 0) return -1;
    KASSERT_NOT_IRQ();
    scheduler_preempt_disable();
    uint32_t flags = process_table_lock_irqsave();
    bool authorized = false;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        Process *target = &process_list[index];
        if (target->is_running && target->pid == pid &&
            target->parent_pid == requester->pid &&
            target->parent_generation == requester->generation) {
            authorized = true;
            break;
        }
    }
    process_table_unlock_irqrestore(flags);
    int result = authorized ? process_terminate(pid) : -1;
    scheduler_preempt_enable();
    return result;
}

int process_get_identity(int pid, uint32_t *generation_out) {
    if (pid <= 0 || generation_out == NULL) return -1;
    uint32_t flags = process_table_lock_irqsave();
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        if (process_list[index].is_running && process_list[index].pid == pid) {
            *generation_out = process_list[index].generation;
            process_table_unlock_irqrestore(flags);
            return 0;
        }
    }
    process_table_unlock_irqrestore(flags);
    return -1;
}

bool process_file_object_owner_live(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return false;
    uint32_t flags = process_table_lock_irqsave();
    bool alive = false;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        const Process* process = &process_list[index];
        if (process->is_running && !process->has_exited && !process->terminating &&
            process->pid == pid && process->generation == generation) {
            alive = true;
            break;
        }
    }
    process_table_unlock_irqrestore(flags);
    return alive;
}

bool process_identity_alive(int pid, uint32_t generation) {
    if (pid <= 0 || generation == 0U) return false;
    uint32_t flags = process_table_lock_irqsave();
    bool alive = false;
    for (int index = 0; index < MAX_PROGRAMS; ++index) {
        if (process_list[index].is_running && process_list[index].pid == pid &&
            process_list[index].generation == generation) {
            alive = true;
            break;
        }
    }
    process_table_unlock_irqrestore(flags);
    return alive;
}
