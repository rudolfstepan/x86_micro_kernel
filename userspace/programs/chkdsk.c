/**
 * @file userspace/programs/chkdsk.c
 * @brief Prüft VFS-Inhalte und vermittelt bestätigte FAT12-Reparaturen.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

#include "reist/vfs_file_client.h"
#include "reist/vfs_read_client.h"
#include "reist/vfs_stat_client.h"

/* Raw-media access and every filesystem mutation remain in the supervised
 * storage service. CHKDSK only validates CLI input and submits bounded,
 * versioned requests from its default-deny maintenance domain. */
#define PATH_CAPACITY 256U
#define READ_CAPACITY 512U
#define MAX_NODES 256U
#define CHKDSK_TIMEOUT_MS 60000U
#define CHKDSK_POLL_MS 10U

typedef struct {
    uint64_t deadline_ms;
} scan_budget_t;

static unsigned scan_failure_stage;

static int scan_fail(unsigned stage) {
    if (scan_failure_stage == 0U) scan_failure_stage = stage;
    return -1;
}

static unsigned length(const char *text) {
    unsigned value = 0;
    while (text[value] != '\0') ++value;
    return value;
}

static int equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left; ++right;
    }
    return *left == *right;
}

static int parse_resource(const char *text, uint32_t *resource) {
    uint32_t value = 0U;
    if (text == 0 || resource == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        uint32_t digit = (uint32_t)(*text - '0');
        if (digit > 9U || value > (UINT32_MAX - digit) / 10U) return -1;
        value = value * 10U + digit;
        ++text;
    }
    *resource = value;
    return 0;
}

static uint64_t monotonic_now(void) {
    uint64_t value = 0U;
    return x86os_monotonic_ms(&value) == 0 ? value : UINT64_MAX;
}

static int scan_remaining_ms(const scan_budget_t *budget,
                             uint32_t *remaining_ms) {
    if (budget == 0 || remaining_ms == 0) return -22;
    *remaining_ms = 0U;
    uint64_t now = monotonic_now();
    if (now == UINT64_MAX || now >= budget->deadline_ms) return -110;
    uint64_t remaining = budget->deadline_ms - now;
    if (remaining > CHKDSK_TIMEOUT_MS) remaining = CHKDSK_TIMEOUT_MS;
    if (remaining == 0U) return -110;
    *remaining_ms = (uint32_t)remaining;
    return 0;
}

static int run_fat12_request(uint32_t operation, uint32_t resource,
                             uint32_t offset, int32_t *operation_result) {
    x86os_storage_submit_t request = {
        .version = X86OS_STORAGE_REQUEST_VERSION,
        .struct_size = sizeof(request),
        .operation = operation,
        .resource = resource,
        .offset = offset,
        .length = 0U,
        .timeout_ms = CHKDSK_TIMEOUT_MS,
    };
    x86os_storage_handle_t handle = 0U;
    int result = x86os_storage_submit(&request, 0, &handle);
    if (result != 0 || handle == 0U) return -1;
    uint64_t start = monotonic_now();
    for (;;) {
        result = x86os_storage_collect(handle, operation_result, 0);
        if (result == 0) return 0;
        uint64_t now = monotonic_now();
        if (result != -11 || start == UINT64_MAX || now == UINT64_MAX ||
            now - start >= CHKDSK_TIMEOUT_MS) return -2;
        if (x86os_sleep_ms(CHKDSK_POLL_MS) != 0) (void)x86os_yield();
    }
}

static int check_fat12(int argc, char **argv) {
    int repair_mirror = argc == 5 && equal(argv[3], "--repair") &&
                        equal(argv[4], "--confirm");
    int repair_chains = argc == 5 && equal(argv[3], "--repair-chains") &&
                        equal(argv[4], "--confirm");
    int repair_short = argc == 5 && equal(argv[3], "--repair-short") &&
                       equal(argv[4], "--confirm");
    int reclaim_orphans = argc == 5 &&
                          equal(argv[3], "--reclaim-orphans") &&
                          equal(argv[4], "--confirm");
    int repair_loops = argc == 5 && equal(argv[3], "--repair-loops") &&
                       equal(argv[4], "--confirm");
    int repair_directory_loops = argc == 5 &&
        equal(argv[3], "--repair-dir-loops") && equal(argv[4], "--confirm");
    int repair_short_loops = argc == 5 &&
        equal(argv[3], "--repair-short-loops") && equal(argv[4], "--confirm");
    int repair_crosslinks = argc == 5 &&
        equal(argv[3], "--repair-crosslinks") && equal(argv[4], "--confirm");
    int repair_directory_size = argc == 5 &&
        equal(argv[3], "--repair-dir-size") && equal(argv[4], "--confirm");
    int repair_volume_label = argc == 5 &&
        equal(argv[3], "--repair-volume-label") && equal(argv[4], "--confirm");
    int repair_zero_files = argc == 5 &&
        equal(argv[3], "--repair-zero-files") && equal(argv[4], "--confirm");
    int repair_zero_start = argc == 5 &&
        equal(argv[3], "--repair-zero-start") && equal(argv[4], "--confirm");
    int repair_dot_size = argc == 5 &&
        equal(argv[3], "--repair-dot-size") && equal(argv[4], "--confirm");
    int repair_dot_cluster = argc == 5 &&
        equal(argv[3], "--repair-dot-cluster") && equal(argv[4], "--confirm");
    int repair_required_crosslinks = argc == 5 &&
        equal(argv[3], "--repair-required-crosslinks") &&
        equal(argv[4], "--confirm");
    int repair_directory_crosslinks = argc == 5 &&
        equal(argv[3], "--repair-directory-crosslinks") &&
        equal(argv[4], "--confirm");
    int repair_directory_topology = argc == 5 &&
        equal(argv[3], "--repair-directory-topology") &&
        equal(argv[4], "--confirm");
    int salvage_orphans = argc == 5 &&
        equal(argv[3], "--salvage-orphans") && equal(argv[4], "--confirm");
    int record_bad_sector = argc == 6 &&
        equal(argv[3], "--record-bad-sector") &&
        equal(argv[5], "--confirm");
    if ((argc != 3 && !repair_mirror && !repair_chains && !repair_short &&
         !reclaim_orphans && !repair_loops && !repair_directory_loops &&
         !repair_short_loops && !repair_crosslinks && !repair_directory_size &&
         !repair_volume_label && !repair_zero_files && !repair_zero_start &&
         !repair_dot_size && !repair_dot_cluster &&
         !repair_required_crosslinks && !repair_directory_crosslinks &&
         !repair_directory_topology && !salvage_orphans &&
         !record_bad_sector) ||
        !equal(argv[1], "--fat12")) {
        x86os_puts("Usage: chkdsk [path]\n"
                   "       chkdsk --fat12 <resource>\n"
                   "       chkdsk --fat12 <resource> --repair --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-chains --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-short --confirm\n"
                   "       chkdsk --fat12 <resource> --reclaim-orphans --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-loops --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dir-loops --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-short-loops --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-crosslinks --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dir-size --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-volume-label --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-zero-files --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-zero-start --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dot-size --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dot-cluster --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-required-crosslinks --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-directory-crosslinks --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-directory-topology --confirm\n"
                   "       chkdsk --fat12 <resource> --salvage-orphans --confirm\n"
                   "       chkdsk --fat12 <resource> --record-bad-sector <sector> --confirm\n");
        return 2;
    }
    uint32_t resource = 0U;
    x86os_drive_info_t drive;
    if (parse_resource(argv[2], &resource) != 0 ||
        x86os_drive_info(resource, &drive) <= 0 ||
        drive.type != X86OS_DRIVE_FDD) {
        x86os_puts("CHKDSK: invalid FAT12 FDD resource; medium unchanged\n");
        return 2;
    }
    uint32_t bad_sector = 0U;
    if (record_bad_sector && parse_resource(argv[4], &bad_sector) != 0) {
        x86os_puts("CHKDSK: invalid FAT12 sector; medium unchanged\n");
        return 2;
    }
    int32_t operation_result = -1;
    uint32_t operation = repair_mirror
        ? X86OS_STORAGE_REPAIR_FAT12_MIRROR
        : repair_chains ? X86OS_STORAGE_REPAIR_FAT12_CHAINS
        : repair_short ? X86OS_STORAGE_REPAIR_FAT12_SHORT_FILES
        : reclaim_orphans ? X86OS_STORAGE_RECLAIM_FAT12_ORPHANS
        : repair_loops ? X86OS_STORAGE_REPAIR_FAT12_LOOPS
        : repair_directory_loops ? X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_LOOPS
        : repair_short_loops ? X86OS_STORAGE_REPAIR_FAT12_SHORT_LOOPS
        : repair_crosslinks ? X86OS_STORAGE_REPAIR_FAT12_CROSSLINKS
        : repair_directory_size ? X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_SIZE
        : repair_volume_label ? X86OS_STORAGE_REPAIR_FAT12_VOLUME_LABEL
        : repair_zero_files ? X86OS_STORAGE_REPAIR_FAT12_ZERO_FILES
        : repair_zero_start ? X86OS_STORAGE_REPAIR_FAT12_ZERO_START_FILES
        : repair_dot_size ? X86OS_STORAGE_REPAIR_FAT12_DOT_SIZE
        : repair_dot_cluster ? X86OS_STORAGE_REPAIR_FAT12_DOT_CLUSTER
        : repair_required_crosslinks
            ? X86OS_STORAGE_REPAIR_FAT12_REQUIRED_CROSSLINKS
        : repair_directory_crosslinks
            ? X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_CROSSLINKS
        : repair_directory_topology
            ? X86OS_STORAGE_REPAIR_FAT12_DIRECTORY_TOPOLOGY
        : salvage_orphans ? X86OS_STORAGE_SALVAGE_FAT12_ORPHANS
        : record_bad_sector ? X86OS_STORAGE_RECORD_FAT12_BAD_SECTOR
                                : X86OS_STORAGE_CHECK_FAT12;
    int request_result = run_fat12_request(operation, resource, bad_sector,
                                           &operation_result);
    if (request_result == -1) {
        x86os_puts("CHKDSK: storage service rejected request; medium unchanged\n");
        return 1;
    }
    if (request_result != 0) {
        x86os_puts("CHKDSK: completion unknown; medium must remain read-only\n");
        return 1;
    }
    if (operation_result < 0) {
        x86os_puts((repair_mirror || repair_chains || repair_short ||
                    reclaim_orphans || repair_loops || repair_directory_loops ||
                    repair_short_loops || repair_crosslinks ||
                    repair_directory_size || repair_volume_label ||
                    repair_zero_files || repair_zero_start || repair_dot_size ||
                    repair_dot_cluster || repair_required_crosslinks ||
                    repair_directory_crosslinks || repair_directory_topology ||
                    salvage_orphans || record_bad_sector)
            ? "CHKDSK: repair refused or failed; medium requires inspection\n"
            : "CHKDSK: FAT12 metadata check failed; medium unchanged\n");
        return 1;
    }
    uint32_t flags = (uint32_t)operation_result;
    if ((flags & X86OS_FAT12_RESULT_MIRROR_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 mirror transaction verified\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_CHAINS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 excess chain transaction verified\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_SHORT_FILES_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 short file transaction verified\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_ORPHANS_RECLAIMED) != 0U) {
        x86os_puts("CHKDSK: unreachable FAT12 allocations discarded\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_LOOPS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 regular-file loops repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_DIRECTORY_LOOPS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 directory loops repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_SHORT_LOOPS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 short regular-file loops repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_CROSSLINKS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 excess-tail crosslinks repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_DIRECTORY_SIZE_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 directory sizes repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_VOLUME_LABEL_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 volume-label fields repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_ZERO_FILES_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 zero-length allocations discarded\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_ZERO_START_FILES_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 zero-start file sizes cleared\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_DOT_SIZE_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 dot-entry sizes repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_DOT_CLUSTER_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 dot-entry clusters repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_REQUIRED_CROSSLINKS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 required crosslinks cloned and repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_DIRECTORY_CROSSLINKS_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 empty directory crosslinks cloned\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_DIRECTORY_TOPOLOGY_REPAIRED) != 0U) {
        x86os_puts("CHKDSK: FAT12 directory topology repaired\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_ORPHANS_SALVAGED) != 0U) {
        x86os_puts("CHKDSK: FAT12 orphan chains saved in FOUND.000\n");
        return 0;
    }
    if ((flags & X86OS_FAT12_RESULT_BAD_SECTOR_REMAPPED) != 0U) {
        x86os_puts("CHKDSK: FAT12 sector copied, verified and remapped\n");
        return 0;
    }
    if (flags == 0U) {
        x86os_puts("CHKDSK: FAT12 BPB and both FAT mirrors are clean\n");
        return 0;
    }
    x86os_puts("CHKDSK: FAT12 consistency flags=");
    x86os_print_number((int)flags);
    x86os_puts((repair_mirror || repair_chains || repair_short ||
                reclaim_orphans || repair_loops || repair_directory_loops ||
                repair_short_loops || repair_crosslinks ||
                repair_directory_size || repair_volume_label ||
                repair_zero_files || repair_zero_start || repair_dot_size ||
                repair_dot_cluster || repair_required_crosslinks ||
                repair_directory_crosslinks || repair_directory_topology ||
                salvage_orphans || record_bad_sector)
        ? "; no repair committed\n"
        : "; inspect flags before explicit repair\n");
    return 1;
}

static int join_path(char *out, const char *parent, const char *name) {
    unsigned at = 0;
    unsigned parent_length = length(parent);
    if (parent_length >= PATH_CAPACITY) return -1;
    for (unsigned i = 0; i < parent_length; ++i) out[at++] = parent[i];
    if (at == 0 || out[at - 1U] != '/') out[at++] = '/';
    for (unsigned i = 0; name[i] != '\0'; ++i) {
        if (at + 1U >= PATH_CAPACITY) return -1;
        out[at++] = name[i];
    }
    out[at] = '\0';
    return 0;
}

static int close_file(reist_vfs_file_handle_t handle,
                      const scan_budget_t *budget) {
    uint32_t remaining = 0U;
    int budget_status = scan_remaining_ms(budget, &remaining);
    if (budget_status != 0) remaining = 1U;
    if (reist_vfs_file_set_timeout(handle, remaining) != 0) return -1;
    int close_status = reist_vfs_file_close(handle);
    return budget_status == 0 && close_status == 0 ? 0 : -1;
}

static int check_file(const char *path, uint32_t expected,
                      const scan_budget_t *budget) {
    char buffer[READ_CAPACITY];
    uint32_t remaining = 0U;
    if (scan_remaining_ms(budget, &remaining) != 0) return scan_fail(10U);
    reist_vfs_file_handle_t handle = REIST_VFS_FILE_INVALID_HANDLE;
    if (reist_vfs_file_open_rights(
            path, remaining,
            REIST_VFS_FILE_RIGHT_READ | REIST_VFS_FILE_RIGHT_STAT,
            &handle) != 0 || handle == REIST_VFS_FILE_INVALID_HANDLE)
        return scan_fail(11U);
    x86os_file_info_t info;
    if (scan_remaining_ms(budget, &remaining) != 0 ||
        reist_vfs_file_set_timeout(handle, remaining) != 0 ||
        reist_vfs_file_fstat(handle, &info) != 0) {
        (void)close_file(handle, budget);
        return scan_fail(12U);
    }
    if (info.type != X86OS_FILE || info.size != expected) {
        (void)close_file(handle, budget);
        return scan_fail(13U);
    }
    uint32_t total = 0U;
    while (total < expected) {
        uint32_t request = expected - total;
        if (request > X86OS_VFS_SHADOW_READ_CAPACITY)
            request = X86OS_VFS_SHADOW_READ_CAPACITY;
        if (request > sizeof(buffer)) request = sizeof(buffer);
        if (scan_remaining_ms(budget, &remaining) != 0 ||
            reist_vfs_file_set_timeout(handle, remaining) != 0) {
            (void)close_file(handle, budget);
            return scan_fail(14U);
        }
        int count = reist_vfs_file_read(handle, buffer, request);
        if (count <= 0 || (uint32_t)count > expected - total) {
            (void)close_file(handle, budget);
            return scan_fail(15U);
        }
        total += (uint32_t)count;
    }
    int eof_status = -1;
    if (scan_remaining_ms(budget, &remaining) == 0 &&
        reist_vfs_file_set_timeout(handle, remaining) == 0)
        eof_status = reist_vfs_file_read(handle, buffer, 1U);
    int close_status = close_file(handle, budget);
    if (eof_status != 0) return scan_fail(16U);
    if (close_status != 0) return scan_fail(17U);
    if (scan_remaining_ms(budget, &remaining) != 0) return scan_fail(18U);
    return 0;
}

static int scan(const char *path, unsigned *visited, unsigned *errors,
                const scan_budget_t *budget) {
    x86os_file_info_t info;
    uint32_t remaining = 0U;
    if (*visited >= MAX_NODES) {
        ++*errors; return scan_fail(1U);
    }
    if (scan_remaining_ms(budget, &remaining) != 0) {
        ++*errors; return scan_fail(2U);
    }
    if (reist_vfs_stat(path, &info, remaining) < 0) {
        ++*errors; return scan_fail(3U);
    }
    ++*visited;
    if (info.type == X86OS_FILE) {
        if (check_file(path, info.size, budget) < 0) {
            ++*errors;
            return -1;
        }
        return 0;
    }
    if (info.type != X86OS_DIRECTORY) {
        ++*errors; return scan_fail(4U);
    }
    for (uint32_t index = 0;;) {
        x86os_file_info_t entry;
        if (scan_remaining_ms(budget, &remaining) != 0) {
            ++*errors; return scan_fail(5U);
        }
        int present = reist_vfs_readdir_at(path, index, &entry, remaining);
        if (present < 0) { ++*errors; return scan_fail(6U); }
        if (present == 0) break;
        ++index;
        if (equal(entry.name, ".") || equal(entry.name, "..")) continue;
        char child[PATH_CAPACITY];
        if (join_path(child, path, entry.name) < 0) {
            ++*errors; return scan_fail(7U);
        }
        if (scan(child, visited, errors, budget) < 0) {
            if (*errors == 0) ++*errors;
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && equal(argv[1], "--fat12"))
        return check_fat12(argc, argv);
    const char *path = argc > 1 ? argv[1] : "/";
    if (argc > 2 || length(path) >= PATH_CAPACITY) {
        x86os_puts("Usage: chkdsk [path]\n"
                   "       chkdsk --fat12 <resource>\n"
                   "       chkdsk --fat12 <resource> --repair --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-chains --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-short --confirm\n"
                   "       chkdsk --fat12 <resource> --reclaim-orphans --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-loops --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dir-loops --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-short-loops --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-crosslinks --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dir-size --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-volume-label --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-zero-files --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-zero-start --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dot-size --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-dot-cluster --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-required-crosslinks --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-directory-crosslinks --confirm\n"
                   "       chkdsk --fat12 <resource> --repair-directory-topology --confirm\n"
                   "       chkdsk --fat12 <resource> --salvage-orphans --confirm\n"
                   "       chkdsk --fat12 <resource> --record-bad-sector <sector> --confirm\n");
        return 2;
    }
    uint64_t start = monotonic_now();
    if (start == UINT64_MAX || UINT64_MAX - start < CHKDSK_TIMEOUT_MS) {
        scan_failure_stage = 8U;
        x86os_puts("CHKDSK: read-only check failed; medium left unchanged\n");
        x86os_puts("CHKDSK: failure stage=8\n");
        return 1;
    }
    scan_budget_t budget = {start + CHKDSK_TIMEOUT_MS};
    unsigned visited = 0, errors = 0;
    (void)scan(path, &visited, &errors, &budget);
    if (errors != 0) {
        x86os_puts("CHKDSK: read-only check failed; medium left unchanged\n");
        x86os_puts("CHKDSK: failure stage=");
        x86os_print_number((int)scan_failure_stage);
        x86os_putchar('\n');
        return 1;
    }
    x86os_puts("CHKDSK: read-only check passed\n");
    return 0;
}
