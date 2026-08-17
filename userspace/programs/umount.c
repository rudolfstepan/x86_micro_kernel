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

int main(int argc, char **argv) {
    if (argc != 2) {
        x86os_puts("Usage: umount <resource>\n");
        return 2;
    }
    x86os_admin_storage_request_t request = {0};
    x86os_admin_storage_result_t result;
    if (parse_resource(argv[1], &request.resource) != 0) return 2;
    request.version = X86OS_ADMIN_MAINTENANCE_VERSION;
    request.struct_size = sizeof(request);
    request.command = X86OS_ADMIN_STORAGE_UMOUNT;
    request.drain_timeout_ms = 500U;
    int code = x86os_admin_storage(&request, &result);
    if (code != 0) {
        x86os_puts(code == -1001 ? "ADMIN ROOT_PROTECTED\n"
                                 : "ADMIN UMOUNT_FAILED\n");
        return 1;
    }
    x86os_puts("ADMIN UMOUNT_OK resource=");
    x86os_print_number((int)request.resource);
    x86os_putchar('\n');
    return 0;
}
