#include "x86os.h"

#define FORMAT_TIMEOUT_MS 60000U
#define FORMAT_POLL_MS 10U
#define FORMAT_FULL_SECTOR_BUDGET_MS 100U

static int equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
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

static uint64_t full_deadline(uint64_t now, uint32_t sectors) {
    uint64_t budget = (uint64_t)sectors * FORMAT_FULL_SECTOR_BUDGET_MS;
    if (budget < FORMAT_TIMEOUT_MS) budget = FORMAT_TIMEOUT_MS;
    return UINT64_MAX - now < budget ? UINT64_MAX : now + budget;
}

static int run_request(uint32_t operation, uint32_t resource,
                       uint32_t offset, int32_t *operation_result) {
    x86os_storage_submit_t request = {
        .version = X86OS_STORAGE_REQUEST_VERSION,
        .struct_size = sizeof(request),
        .operation = operation,
        .resource = resource,
        .offset = offset,
        .length = 0U,
        .timeout_ms = FORMAT_TIMEOUT_MS,
    };
    x86os_storage_handle_t handle = 0U;
    int result = x86os_storage_submit(&request, 0, &handle);
    if (result != 0 || handle == 0U) return -1;
    uint64_t start = monotonic_now();
    for (;;) {
        result = x86os_storage_collect(handle, operation_result, 0);
        if (result == 0) return 0;
        uint64_t now = monotonic_now();
        if (result != -11 || now == UINT64_MAX || start == UINT64_MAX ||
            now - start >= FORMAT_TIMEOUT_MS) return -2;
        if (x86os_sleep_ms(FORMAT_POLL_MS) != 0) (void)x86os_yield();
    }
}

static int prepare_fat32(uint32_t resource, uint32_t sectors) {
    uint32_t cursor = 0U;
    uint32_t chunks = 0U;
    uint32_t max_chunks = sectors / (128U * 256U) + 2U;
    do {
        int32_t operation_result = -1;
        if (chunks++ >= max_chunks ||
            run_request(X86OS_STORAGE_FORMAT_FAT32_PREPARE, resource,
                        cursor, &operation_result) != 0 ||
            operation_result < 0) return -1;
        uint32_t next = (uint32_t)operation_result;
        if (next != 0U && next <= cursor) return -1;
        cursor = next;
    } while (cursor != 0U);
    return 0;
}

int main(int argc, char **argv) {
    int fat12 = argc == 4 && equal(argv[1], "--reist-fat12");
    int fat32 = argc == 5 && equal(argv[1], "--reist-fat32");
    int quick = fat32 && equal(argv[2], "--quick");
    int full = fat32 && equal(argv[2], "--full");
    uint32_t resource_argument = fat12 ? 2U : 3U;
    uint32_t confirm_argument = fat12 ? 3U : 4U;
    if ((!fat12 && (!fat32 || (!quick && !full))) ||
        !equal(argv[confirm_argument], "--confirm")) {
        x86os_puts("Usage: format --reist-fat12 <resource> --confirm\n"
                   "       format --reist-fat32 --quick|--full <resource> --confirm\n");
        return 2;
    }
    uint32_t resource = 0U;
    if (parse_resource(argv[resource_argument], &resource) != 0) {
        x86os_puts("FORMAT: invalid resource id; medium unchanged\n");
        return 2;
    }
    x86os_drive_info_t drive;
    int drive_result = x86os_drive_info(resource, &drive);
    if (drive_result <= 0 || (!fat12 && drive.type != X86OS_DRIVE_PARTITION) ||
        (fat12 && drive.type != X86OS_DRIVE_FDD)) {
        x86os_puts("FORMAT: resource has the wrong type; medium unchanged\n");
        return 1;
    }

    if (fat32 && prepare_fat32(resource, drive.sectors) != 0) {
        x86os_puts("FORMAT: FAT initialization failed; medium must remain read-only\n");
        return 1;
    }
    int32_t operation_result = -1;
    uint32_t operation = fat32 ? X86OS_STORAGE_FORMAT_FAT32 :
                                 X86OS_STORAGE_FORMAT_FAT12;
    int result = run_request(operation, resource, 0U, &operation_result);
    if (result == -1) {
        x86os_puts("FORMAT: service rejected request; medium unchanged\n");
        return 1;
    }
    if (result != 0) {
        x86os_puts("FORMAT: completion unknown; medium must remain read-only\n");
        return 1;
    }
    if (operation_result != 0) {
        x86os_puts("FORMAT: operation failed; medium requires inspection\n");
        return 1;
    }
    if (fat12) {
        x86os_puts("FORMAT: REIST FAT12 format verified\n");
        return 0;
    }
    if (quick) {
        x86os_puts("FORMAT: REIST FAT32 quick format verified\n");
        return 0;
    }

    uint64_t deadline = full_deadline(monotonic_now(), drive.sectors);
    uint32_t cluster = 3U;
    uint32_t chunks = 0U;
    uint32_t max_chunks = drive.sectors / 256U + 2U;
    while (cluster != 0U && chunks++ < max_chunks) {
        uint64_t now = monotonic_now();
        if (now == UINT64_MAX || now >= deadline) {
            x86os_puts("FORMAT: full scan deadline expired; medium must remain read-only\n");
            return 1;
        }
        operation_result = -1;
        result = run_request(X86OS_STORAGE_FORMAT_FAT32_SCAN, resource,
                             cluster, &operation_result);
        if (result != 0 || operation_result < 0) {
            x86os_puts("FORMAT: full scan failed; medium requires inspection\n");
            return 1;
        }
        uint32_t next = (uint32_t)operation_result;
        if (next != 0U && (next <= cluster || next > drive.sectors + 1U)) {
            x86os_puts("FORMAT: invalid scan progress; medium must remain read-only\n");
            return 1;
        }
        cluster = next;
    }
    if (cluster != 0U) {
        x86os_puts("FORMAT: scan bound exceeded; medium must remain read-only\n");
        return 1;
    }
    x86os_puts("FORMAT: REIST FAT32 full format and bad-cluster blacklist verified\n");
    return 0;
}
