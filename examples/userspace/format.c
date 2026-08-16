#include "x86os.h"

#define FORMAT_TIMEOUT_MS 30000U
#define FORMAT_POLL_MS 10U

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

int main(int argc, char **argv) {
    if (argc != 4 || !equal(argv[1], "--reist-fat12") ||
        !equal(argv[3], "--confirm")) {
        x86os_puts("Usage: format --reist-fat12 <resource> --confirm\n");
        return 2;
    }
    uint32_t resource = 0U;
    if (parse_resource(argv[2], &resource) != 0) {
        x86os_puts("FORMAT: invalid resource id; medium unchanged\n");
        return 2;
    }
    x86os_drive_info_t drive;
    int drive_result = x86os_drive_info(resource, &drive);
    if (drive_result <= 0 || drive.type != X86OS_DRIVE_FDD) {
        x86os_puts("FORMAT: resource is not an available FDD; medium unchanged\n");
        return 1;
    }

    x86os_storage_submit_t request = {
        .version = X86OS_STORAGE_REQUEST_VERSION,
        .struct_size = sizeof(request),
        .operation = X86OS_STORAGE_FORMAT_FAT12,
        .resource = resource,
        .offset = 0U,
        .length = 0U,
        .timeout_ms = FORMAT_TIMEOUT_MS,
    };
    x86os_storage_handle_t handle = 0U;
    int result = x86os_storage_submit(&request, 0, &handle);
    if (result != 0 || handle == 0U) {
        x86os_puts("FORMAT: service rejected request; medium unchanged\n");
        return 1;
    }
    uint64_t start = monotonic_now();
    for (;;) {
        int32_t operation_result = -1;
        result = x86os_storage_collect(handle, &operation_result, 0);
        if (result == 0) {
            if (operation_result == 0) {
                x86os_puts("FORMAT: REIST FAT12 format verified\n");
                return 0;
            }
            x86os_puts("FORMAT: operation failed; medium requires inspection\n");
            return 1;
        }
        uint64_t now = monotonic_now();
        if (result != -11 || now == UINT64_MAX || now - start >= FORMAT_TIMEOUT_MS) {
            x86os_puts("FORMAT: completion unknown; medium must remain read-only\n");
            return 1;
        }
        if (x86os_sleep_ms(FORMAT_POLL_MS) != 0) (void)x86os_yield();
    }
}
