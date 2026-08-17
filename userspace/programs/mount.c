#include "x86os.h"

static int parse_resource(const char *text, uint32_t *resource) {
    if (text == 0 || resource == 0 || *text == '\0') return -1;
    uint32_t value = 0U;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9' || value > 999U) return -1;
        value = value * 10U + (uint32_t)(*text - '0');
    }
    *resource = value;
    return 0;
}

static int copy_text(char *target, uint32_t capacity, const char *source) {
    uint32_t index = 0U;
    while (source[index] != '\0') {
        if (index + 1U >= capacity) return -1;
        target[index] = source[index];
        ++index;
    }
    target[index] = '\0';
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        x86os_puts("Usage: mount <resource> <fat12|fat32|ext2> <path>\n");
        return 2;
    }
    x86os_admin_storage_request_t request = {0};
    x86os_admin_storage_result_t result;
    if (parse_resource(argv[1], &request.resource) != 0 ||
        copy_text(request.fs_type, sizeof(request.fs_type), argv[2]) != 0 ||
        copy_text(request.mount_path, sizeof(request.mount_path), argv[3]) != 0)
        return 2;
    request.version = X86OS_ADMIN_MAINTENANCE_VERSION;
    request.struct_size = sizeof(request);
    request.command = X86OS_ADMIN_STORAGE_MOUNT;
    request.drain_timeout_ms = 500U;
    int code = x86os_admin_storage(&request, &result);
    if (code != 0) {
        x86os_puts(code == -1001 ? "ADMIN ROOT_PROTECTED\n"
                                 : "ADMIN MOUNT_FAILED\n");
        return 1;
    }
    x86os_puts("ADMIN MOUNT_OK resource=");
    x86os_print_number((int)request.resource);
    x86os_puts(" path=");
    x86os_puts(request.mount_path);
    x86os_putchar('\n');
    return 0;
}
