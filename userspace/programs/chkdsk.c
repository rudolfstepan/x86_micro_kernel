/**
 * @file userspace/programs/chkdsk.c
 * @brief Prüft VFS-Inhalte und vermittelt bestätigte FAT12-Reparaturen.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

/* Raw-media access and every filesystem mutation remain in the supervised
 * storage service. CHKDSK only validates CLI input and submits bounded,
 * versioned requests from its default-deny maintenance domain. */
#define PATH_CAPACITY 256U
#define READ_CAPACITY 512U
#define MAX_NODES 256U
#define CHKDSK_TIMEOUT_MS 60000U
#define CHKDSK_POLL_MS 10U

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

static int run_fat12_request(uint32_t operation, uint32_t resource,
                             int32_t *operation_result) {
    x86os_storage_submit_t request = {
        .version = X86OS_STORAGE_REQUEST_VERSION,
        .struct_size = sizeof(request),
        .operation = operation,
        .resource = resource,
        .offset = 0U,
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
    if ((argc != 3 && !repair_mirror && !repair_chains && !repair_short &&
         !reclaim_orphans && !repair_loops && !repair_directory_loops &&
         !repair_short_loops && !repair_crosslinks && !repair_directory_size &&
         !repair_volume_label) ||
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
                   "       chkdsk --fat12 <resource> --repair-volume-label --confirm\n");
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
                                : X86OS_STORAGE_CHECK_FAT12;
    int request_result = run_fat12_request(operation, resource,
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
                    repair_directory_size || repair_volume_label)
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
    if (flags == 0U) {
        x86os_puts("CHKDSK: FAT12 BPB and both FAT mirrors are clean\n");
        return 0;
    }
    x86os_puts("CHKDSK: FAT12 consistency flags=");
    x86os_print_number((int)flags);
    x86os_puts((repair_mirror || repair_chains || repair_short ||
                reclaim_orphans || repair_loops || repair_directory_loops ||
                repair_short_loops || repair_crosslinks ||
                repair_directory_size || repair_volume_label)
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

static int check_file(const char *path, uint32_t expected) {
    char buffer[READ_CAPACITY];
    int descriptor = x86os_open(path);
    if (descriptor < 0) return -1;
    uint32_t total = 0;
    for (;;) {
        int count = x86os_read(descriptor, buffer, sizeof(buffer));
        if (count < 0 || total > expected || (uint32_t)count > expected - total) {
            (void)x86os_close(descriptor);
            return -1;
        }
        if (count == 0) break;
        total += (uint32_t)count;
    }
    if (x86os_close(descriptor) < 0 || total != expected) return -1;
    return 0;
}

static int scan(const char *path, unsigned *visited, unsigned *errors) {
    x86os_file_info_t info;
    if (*visited >= MAX_NODES || x86os_stat(path, &info) < 0) {
        ++*errors; return -1;
    }
    ++*visited;
    if (info.type == X86OS_FILE) {
        if (check_file(path, info.size) < 0) ++*errors;
        return 0;
    }
    for (uint32_t index = 0;;) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, index, entries);
        if (count < 0) { ++*errors; return -1; }
        if (count == 0) break;
        for (int item = 0; item < count; ++item) {
            if (equal(entries[item].name, ".") || equal(entries[item].name, ".."))
                continue;
            char child[PATH_CAPACITY];
            if (join_path(child, path, entries[item].name) < 0 ||
                scan(child, visited, errors) < 0) {
                if (*errors == 0) ++*errors;
            }
        }
        index += (uint32_t)count;
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
                   "       chkdsk --fat12 <resource> --repair-volume-label --confirm\n");
        return 2;
    }
    unsigned visited = 0, errors = 0;
    (void)scan(path, &visited, &errors);
    if (errors != 0) {
        x86os_puts("CHKDSK: read-only check failed; medium left unchanged\n");
        return 1;
    }
    x86os_puts("CHKDSK: read-only check passed\n");
    return 0;
}
