#include "include/kernel/storage_service.h"

#include "arch/x86/include/interrupt.h"
#include "drivers/block/ata.h"
#include "drivers/block/ahci.h"
#include "drivers/block/block_device.h"
#include "drivers/block/fdd.h"
#include "include/kernel/critical_object.h"
#include "include/kernel/filesystem_safety.h"
#include "include/kernel/storage_request_pool.h"
#include "include/kernel/storage_safety.h"
#include "include/kernel/supervisor.h"
#include "kernel/proc/process.h"
#include "lib/libc/stdio.h"
#include "lib/libc/string.h"
#include "kernel/time/pit.h"

#define STORAGE_SERVICE_CONTROL_VERSION 1U
#define STORAGE_SERVICE_START_TIMEOUT_MS 1000U
#define STORAGE_SERVICE_RESTART_BUDGET 3U
#define STORAGE_MEDIA_PROBE_INITIAL_MS 250U
#define STORAGE_MEDIA_PROBE_MAX_MS 30000U
#define STORAGE_MEDIA_FINGERPRINT_VERSION 3U

typedef struct {
    int32_t pid;
    uint32_t generation;
    uint32_t launch_count;
    uint32_t healthy;
    uint32_t quarantined_resources;
    uint32_t read_only_resources;
    uint32_t probe_cursor;
    uint32_t probe_attempts;
    uint64_t start_deadline_ms;
    uint64_t next_probe_ms;
} storage_service_control_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint16_t base;
    uint8_t is_master;
    uint8_t fdd_drive_no;
    uint32_t sectors;
    uint32_t cylinder;
    uint32_t head;
    uint32_t sector;
    uint32_t boot_crc32;
    uint32_t lba_offset;
    uint8_t parent_resource;
    uint8_t partition_type;
    uint8_t partition_index;
    uint8_t partition_scheme;
    uint32_t partition_guid_crc32;
    char model_prefix[24];
} storage_media_fingerprint_t;

static critical_object_t protected_control;
static bool initialized;
static volatile bool service_starting;
static volatile bool service_started;
static critical_object_t media_fingerprints[MAX_DRIVES];
static uint32_t fingerprint_ready_mask;

_Static_assert(sizeof(storage_service_control_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "storage service control exceeds protected payload");
_Static_assert(MAX_DRIVES > 0 && MAX_DRIVES < 32,
               "storage quarantine mask requires 1..31 resources");
_Static_assert(sizeof(storage_media_fingerprint_t) <=
                   CRITICAL_OBJECT_MAX_PAYLOAD,
               "storage fingerprint exceeds protected payload");

static bool control_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(storage_service_control_t))
        return false;
    const storage_service_control_t *control = payload;
    if (control->healthy > 1U ||
        ((control->quarantined_resources | control->read_only_resources) &
         ~((1U << MAX_DRIVES) - 1U)) != 0U ||
        control->probe_cursor >= MAX_DRIVES ||
        control->launch_count > STORAGE_SERVICE_RESTART_BUDGET + 1U)
        return false;
    if (control->pid == 0)
        return control->generation == 0U && control->healthy == 0U &&
               control->start_deadline_ms == 0U;
    return control->pid > 0 && control->generation != 0U &&
           (control->healthy != 0U || control->start_deadline_ms != 0U);
}

static uint32_t media_crc32(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t partition_guid_crc32(const drive_t *drive) {
    uint8_t guids[32];
    if (drive == NULL || drive->partition_scheme != PARTITION_SCHEME_GPT)
        return 0U;
    memcpy(guids, drive->partition_type_guid, 16U);
    memcpy(guids + 16U, drive->partition_guid, 16U);
    return media_crc32(guids, sizeof(guids));
}

static void canonical_model_prefix(char output[24], const char *model) {
    size_t length = 0U;
    memset(output, 0, 24U);
    if (model == NULL) return;
    while (length < 24U && model[length] != '\0') {
        output[length] = model[length];
        ++length;
    }
    while (length > 0U && output[length - 1U] == ' ')
        output[--length] = '\0';
}

static bool fingerprint_valid(const void *payload, size_t length) {
    if (payload == NULL || length != sizeof(storage_media_fingerprint_t))
        return false;
    const storage_media_fingerprint_t *fingerprint = payload;
    return fingerprint->type == DRIVE_TYPE_ATA ||
           fingerprint->type == DRIVE_TYPE_FDD ||
           fingerprint->type == DRIVE_TYPE_AHCI ||
           (fingerprint->type == DRIVE_TYPE_PARTITION &&
            fingerprint->parent_resource < MAX_DRIVES &&
            fingerprint->partition_index >= 1U &&
            ((fingerprint->partition_scheme == PARTITION_SCHEME_MBR &&
              fingerprint->partition_type != 0U &&
              fingerprint->partition_index <= 4U) ||
             (fingerprint->partition_scheme == PARTITION_SCHEME_GPT &&
              fingerprint->partition_index <= 128U)));
}

static bool read_boot_sector(uint32_t resource, uint8_t *data,
                             bool recovery_probe) {
    if (resource >= (uint32_t)drive_count || data == NULL) return false;
    drive_t *drive = &detected_drives[resource];
    if (drive->type == DRIVE_TYPE_FDD) {
        if (recovery_probe)
            return fdc_read_sector_recovery(drive->fdd_drive_no, 0U, 0U, 1U,
                                            data);
        return fdc_read_sector(drive->fdd_drive_no, 0U, 0U, 1U, data);
    }
    if (drive->type == DRIVE_TYPE_ATA ||
        drive->type == DRIVE_TYPE_AHCI ||
        drive->type == DRIVE_TYPE_PARTITION) {
        if (recovery_probe)
            return ata_read_sector_fresh(drive->base, 0U, data,
                                         drive->is_master);
        return block_device_read_sector(drive, 0U, data) == BLOCK_DEVICE_OK;
    }
    return false;
}

static bool capture_fingerprint(uint32_t resource) {
    if (resource >= (uint32_t)drive_count || resource >= MAX_DRIVES)
        return false;
    drive_t *drive = &detected_drives[resource];
    uint8_t first[SECTOR_SIZE], second[SECTOR_SIZE];
    if (!read_boot_sector(resource, first, false) ||
        !read_boot_sector(resource, second, false) ||
        memcmp(first, second, sizeof(first)) != 0) return false;
    storage_media_fingerprint_t fingerprint = {
        .type = drive->type,
        .base = drive->base,
        .is_master = drive->is_master ? 1U : 0U,
        .fdd_drive_no = drive->fdd_drive_no,
        .sectors = drive->sectors,
        .cylinder = drive->cylinder,
        .head = drive->head,
        .sector = drive->sector,
        .boot_crc32 = media_crc32(first, sizeof(first)),
        .lba_offset = drive->lba_offset,
        .parent_resource = drive->parent_resource,
        .partition_type = drive->partition_type,
        .partition_index = drive->partition_index,
        .partition_scheme = drive->partition_scheme,
    };
    fingerprint.partition_guid_crc32 = partition_guid_crc32(drive);
    canonical_model_prefix(fingerprint.model_prefix, drive->model);
    if (critical_object_init(&media_fingerprints[resource],
            STORAGE_MEDIA_FINGERPRINT_VERSION, &fingerprint,
            sizeof(fingerprint)) != 0) return false;
    fingerprint_ready_mask |= 1U << resource;
    return true;
}

static bool media_identity_matches(uint32_t resource) {
#ifdef REIST_STORAGE_IO_FAULT_INJECTION
#define MEDIA_PROBE_FAIL(stage) do { \
    printf("REIST_STORAGE PROBE_FAIL_%s %u\n", stage, resource); \
    return false; \
} while (0)
#else
#define MEDIA_PROBE_FAIL(stage) return false
#endif
    if (resource >= (uint32_t)drive_count || resource >= MAX_DRIVES ||
        (fingerprint_ready_mask & (1U << resource)) == 0U)
        MEDIA_PROBE_FAIL("RANGE");
    storage_media_fingerprint_t expected;
    size_t length = 0U;
    if (critical_object_read(&media_fingerprints[resource],
            STORAGE_MEDIA_FINGERPRINT_VERSION, &expected, sizeof(expected),
            &length, fingerprint_valid) < 0 || length != sizeof(expected))
        MEDIA_PROBE_FAIL("FINGERPRINT");
    drive_t *drive = &detected_drives[resource];
    if ((uint32_t)drive->type != expected.type) MEDIA_PROBE_FAIL("TYPE");
    if (drive->type == DRIVE_TYPE_ATA) {
        drive_t identified;
        char model_prefix[24];
        memset(&identified, 0, sizeof(identified));
        uint8_t selector = drive->is_master ? ATA_MASTER : ATA_SLAVE;
        if (!ata_identify_drive(drive->base, selector, &identified))
            MEDIA_PROBE_FAIL("ATA_IDENTITY");
        canonical_model_prefix(model_prefix, identified.model);
        if (identified.sectors != expected.sectors ||
            memcmp(model_prefix, expected.model_prefix,
                   sizeof(model_prefix)) != 0)
            MEDIA_PROBE_FAIL("ATA_IDENTITY");
    } else if (drive->type == DRIVE_TYPE_FDD) {
        if (drive->fdd_drive_no != expected.fdd_drive_no ||
            drive->cylinder != expected.cylinder ||
            drive->head != expected.head || drive->sector != expected.sector ||
            !fdc_requalify_drive(drive->fdd_drive_no))
            MEDIA_PROBE_FAIL("FDD_IDENTITY");
    } else if (drive->type == DRIVE_TYPE_AHCI) {
        char model_prefix[24];
        if (!ahci_requalify_drive(drive)) MEDIA_PROBE_FAIL("AHCI_RESET");
        canonical_model_prefix(model_prefix, drive->model);
        if (drive->sectors != expected.sectors ||
            memcmp(model_prefix, expected.model_prefix,
                   sizeof(model_prefix)) != 0)
            MEDIA_PROBE_FAIL("AHCI_IDENTITY");
    } else if (drive->type == DRIVE_TYPE_PARTITION) {
        if (drive->parent_resource != expected.parent_resource ||
            drive->partition_type != expected.partition_type ||
            drive->partition_index != expected.partition_index ||
            drive->partition_scheme != expected.partition_scheme ||
            partition_guid_crc32(drive) != expected.partition_guid_crc32 ||
            drive->lba_offset != expected.lba_offset ||
            drive->sectors != expected.sectors ||
            drive->parent_resource >= (uint32_t)drive_count ||
            detected_drives[drive->parent_resource].type ==
                DRIVE_TYPE_PARTITION)
            MEDIA_PROBE_FAIL("PARTITION_IDENTITY");
    } else {
        MEDIA_PROBE_FAIL("TYPE");
    }
    uint8_t first[SECTOR_SIZE], second[SECTOR_SIZE];
    bool result = read_boot_sector(resource, first, true) &&
           read_boot_sector(resource, second, true) &&
           memcmp(first, second, sizeof(first)) == 0 &&
           media_crc32(first, sizeof(first)) == expected.boot_crc32;
    if (!result) MEDIA_PROBE_FAIL("MEDIA");
#undef MEDIA_PROBE_FAIL
    return true;
}

static int control_read(storage_service_control_t *control) {
    size_t length = 0U;
    uint32_t flags = irq_save();
    int result = critical_object_read(&protected_control,
        STORAGE_SERVICE_CONTROL_VERSION, control, sizeof(*control), &length,
        control_valid) < 0 || length != sizeof(*control) ? -84 : 0;
    irq_restore(flags);
    return result;
}

static int control_write(const storage_service_control_t *control) {
    uint32_t flags = irq_save();
    int result = critical_object_update(&protected_control,
        STORAGE_SERVICE_CONTROL_VERSION, control, sizeof(*control),
        control_valid) == 0 ? 0 : -84;
    irq_restore(flags);
    return result;
}

static uint64_t deadline_after(uint64_t now_ms, uint32_t interval_ms) {
    return UINT64_MAX - now_ms < interval_ms
        ? UINT64_MAX : now_ms + interval_ms;
}

static bool spawn_service(storage_service_control_t *control,
                          uint64_t now_ms) {
    const char *arguments[] = {"STORAGE.PRG"};
    int pid = supervisor_spawn_service("/STORAGE.PRG", 1, arguments,
                                       PROCESS_DOMAIN_STORAGE);
    uint32_t generation = 0U;
    if (pid <= 0 || process_get_identity(pid, &generation) != 0) return false;
    control->pid = pid;
    control->generation = generation;
    control->healthy = 0U;
    ++control->launch_count;
    control->start_deadline_ms = deadline_after(
        now_ms, STORAGE_SERVICE_START_TIMEOUT_MS);
    if (control_write(control) != 0) {
        (void)process_terminate(pid);
        return false;
    }
    return true;
}

bool storage_service_init(void) {
    storage_service_control_t control = {0};
    if (critical_object_init(&protected_control,
            STORAGE_SERVICE_CONTROL_VERSION, &control,
            sizeof(control)) != 0) return false;
    initialized = true;
    return true;
}

bool storage_service_inventory_media(void) {
    if (!initialized || drive_count < 0 || drive_count > MAX_DRIVES)
        return false;
    storage_service_control_t control;
    if (control_read(&control) != 0) return false;
    for (uint32_t resource = 0U; resource < (uint32_t)drive_count; ++resource) {
        if (capture_fingerprint(resource)) continue;
        control.quarantined_resources |= 1U << resource;
        control.read_only_resources |= 1U << resource;
    }
    control.probe_cursor = 0U;
    control.probe_attempts = 0U;
    control.next_probe_ms = control.quarantined_resources != 0U
        ? deadline_after(pit_monotonic_ms(), STORAGE_MEDIA_PROBE_INITIAL_MS)
        : 0U;
    return control_write(&control) == 0;
}

bool storage_service_start(uint64_t now_ms) {
    if (!initialized || service_starting || service_started) return false;
    service_starting = true;
    storage_service_control_t control;
    bool result = control_read(&control) == 0 && control.pid == 0 &&
                  spawn_service(&control, now_ms);
    service_started = result;
    service_starting = false;
    return result;
}

int storage_service_bind(int pid, uint32_t generation) {
    storage_service_control_t control;
    if (!initialized || control_read(&control) != 0 || control.pid != pid ||
        control.generation != generation || control.healthy != 0U ||
        !process_identity_alive(pid, generation)) return -13;
    int result = storage_request_bind_service(pid, generation);
    if (result != 0) return result;
    control.healthy = 1U;
    control.start_deadline_ms = 0U;
    if (control_write(&control) != 0) {
        storage_request_unbind_service(pid, generation);
        return -84;
    }
    printf("REIST_STORAGE SERVICE_READY\n");
    return 0;
}

bool storage_service_authorized(int pid, uint32_t generation) {
    storage_service_control_t control;
    return initialized && control_read(&control) == 0 &&
           control.healthy != 0U && control.pid == pid &&
           control.generation == generation &&
           process_identity_alive(pid, generation);
}

bool storage_service_resource_available(uint32_t resource) {
    storage_service_control_t control;
    if (resource >= MAX_DRIVES || !initialized ||
        control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return false;
    }
    return (control.quarantined_resources & (1U << resource)) == 0U;
}

bool storage_service_resource_read_only(uint32_t resource) {
    storage_service_control_t control;
    if (resource >= MAX_DRIVES || !initialized ||
        control_read(&control) != 0) return true;
    return (control.read_only_resources & (1U << resource)) != 0U;
}

bool storage_service_media_fingerprint(uint32_t resource, uint32_t *fingerprint) {
    if (fingerprint == NULL || resource >= MAX_DRIVES || !initialized ||
        !media_identity_matches(resource)) return false;
    storage_media_fingerprint_t value;
    size_t length = 0U;
    if ((fingerprint_ready_mask & (1U << resource)) == 0U ||
        critical_object_read(&media_fingerprints[resource],
            STORAGE_MEDIA_FINGERPRINT_VERSION, &value, sizeof(value),
            &length, fingerprint_valid) < 0 || length != sizeof(value))
        return false;
    *fingerprint = value.boot_crc32;
    return *fingerprint != 0U;
}

bool storage_service_report_io_failure(uint32_t resource) {
    return storage_service_report_media_failure(resource, false);
}

bool storage_service_report_media_failure(uint32_t resource,
                                          bool write_uncertain) {
    storage_service_control_t control;
    if (resource >= MAX_DRIVES || !initialized ||
        control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return false;
    }
    uint32_t mask = 1U << resource;
    if ((control.quarantined_resources & mask) == 0U) {
        control.quarantined_resources |= mask;
        printf("REIST_STORAGE RESOURCE_QUARANTINED %u\n", resource);
    }
    if (write_uncertain) control.read_only_resources |= mask;
    control.probe_attempts = 0U;
    control.next_probe_ms = deadline_after(
        pit_monotonic_ms(), STORAGE_MEDIA_PROBE_INITIAL_MS);
    if (control_write(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return false;
    }
    if (write_uncertain) {
        storage_fence_writes();
        filesystem_fence_mutations();
    }
    return true;
}

static void poll_media_reintegration(uint64_t now_ms) {
    storage_service_control_t control;
    if (control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return;
    }
    if (control.quarantined_resources == 0U ||
        now_ms < control.next_probe_ms) return;
    uint32_t resource = 0U;
    bool found = false;
    for (uint32_t count = 0U; count < MAX_DRIVES; ++count) {
        uint32_t candidate = (control.probe_cursor + count) % MAX_DRIVES;
        if ((control.quarantined_resources & (1U << candidate)) == 0U)
            continue;
        resource = candidate;
        found = true;
        break;
    }
    if (!found) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return;
    }
    bool recovered = media_identity_matches(resource);
    if (control_read(&control) != 0 ||
        (control.quarantined_resources & (1U << resource)) == 0U) return;
    if (recovered) {
        uint32_t mask = 1U << resource;
        if ((control.read_only_resources & mask) != 0U) {
            uint32_t other_unsafe =
                (control.quarantined_resources |
                 control.read_only_resources) & ~mask;
            recovered = other_unsafe == 0U &&
                ata_journal_recover_resource(resource) &&
                storage_restore_writes_after_recovery() &&
                filesystem_restore_mutations_after_recovery();
            if (!recovered) {
                storage_fence_writes();
                filesystem_fence_mutations();
            } else {
                control.read_only_resources &= ~mask;
            }
        }
        if (recovered) {
            control.quarantined_resources &= ~mask;
            control.probe_attempts = 0U;
            printf("REIST_STORAGE RESOURCE_REINTEGRATED_RW %u\n", resource);
        }
    }
    if (!recovered && control.probe_attempts != UINT32_MAX) {
        ++control.probe_attempts;
    }
    control.probe_cursor = (resource + 1U) % MAX_DRIVES;
    uint32_t shift = control.probe_attempts > 6U
        ? 6U : control.probe_attempts;
    uint32_t interval = STORAGE_MEDIA_PROBE_INITIAL_MS << shift;
    if (interval > STORAGE_MEDIA_PROBE_MAX_MS)
        interval = STORAGE_MEDIA_PROBE_MAX_MS;
    control.next_probe_ms = control.quarantined_resources != 0U
        ? deadline_after(now_ms, interval) : 0U;
    if (control_write(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
    }
}

void storage_service_poll(uint64_t now_ms) {
    if (!initialized) return;
    poll_media_reintegration(now_ms);
    if (!service_started || service_starting) return;
    storage_service_control_t control;
    if (control_read(&control) != 0) {
        storage_fence_writes();
        filesystem_fence_mutations();
        return;
    }
    if (control.pid != 0 && process_identity_alive(
            control.pid, control.generation) &&
        (control.healthy != 0U || now_ms < control.start_deadline_ms)) return;

    if (control.pid != 0) {
        storage_request_unbind_service(control.pid, control.generation);
        if (process_identity_alive(control.pid, control.generation))
            (void)process_terminate(control.pid);
        printf("REIST_STORAGE SERVICE_FAILURE_DETECTED\n");
    }
    control.pid = 0;
    control.generation = 0U;
    control.healthy = 0U;
    control.start_deadline_ms = 0U;
    if (control.launch_count > STORAGE_SERVICE_RESTART_BUDGET ||
        !spawn_service(&control, now_ms)) {
        storage_fence_writes();
        filesystem_fence_mutations();
        (void)control_write(&control);
        printf("REIST_STORAGE SERVICE_DEGRADED\n");
    } else if (control.launch_count > 1U) {
        printf("REIST_STORAGE SERVICE_RESTARTED\n");
    }
}
