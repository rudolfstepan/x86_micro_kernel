/**
 * @file userspace/programs/devctl.c
 * @brief Steuert Geräte administrativ down oder up.
 *
 * Layer: Ring-3 system program or command.
 * Contract: Argumente und SDK-Rückgaben werden vor weiteren Operationen validiert.
 * Safety: Ressourcenarbeit ist begrenzt; Fehler werden an die Shell gemeldet und nicht verschleiert.
 */
#include "x86os.h"

#define RESOURCE_LIMIT 32U

static int equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

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

static void print_state(uint32_t state) {
    if (state == X86OS_ADMIN_RESOURCE_ONLINE) x86os_puts("ONLINE");
    else if (state == X86OS_ADMIN_RESOURCE_TRANSITION)
        x86os_puts("TRANSITION");
    else if (state == X86OS_ADMIN_RESOURCE_DOWN) x86os_puts("ADMIN_DOWN");
    else if (state == X86OS_ADMIN_RESOURCE_FAILED) x86os_puts("FAILED");
    else x86os_puts("UNKNOWN");
}

static void print_state_value(uint32_t state) {
    print_state(state);
}

static int status(uint32_t resource, int list_line) {
    x86os_admin_storage_request_t request = {0};
    x86os_admin_storage_result_t result;
    request.version = X86OS_ADMIN_MAINTENANCE_VERSION;
    request.struct_size = sizeof(request);
    request.command = X86OS_ADMIN_STORAGE_STATUS;
    request.resource = resource;
    int code = x86os_admin_storage(&request, &result);
    if (code != 0) return code;
    x86os_puts(list_line ? "ADMIN RESOURCE " : "ADMIN STATUS ");
    x86os_print_number((int)resource);
    x86os_puts(" name=");
    x86os_puts(result.name);
    x86os_puts(" type=");
    if (result.drive_type == X86OS_DRIVE_FDD) x86os_puts("FDD");
    else if (result.drive_type == X86OS_DRIVE_AHCI) x86os_puts("SATA");
    else if (result.drive_type == X86OS_DRIVE_PARTITION)
        x86os_puts("PART");
    else x86os_puts("ATA");
    x86os_puts(" state=");
    print_state(result.state);
    x86os_puts(" mount=");
    x86os_puts((result.flags & X86OS_ADMIN_RESOURCE_MOUNTED) != 0U
        ? result.mount_path : "-");
    if ((result.flags & X86OS_ADMIN_RESOURCE_ROOT) != 0U)
        x86os_puts(" ROOT_PROTECTED");
    if ((result.flags & X86OS_ADMIN_RESOURCE_BLOCKED) != 0U)
        x86os_puts(" BLOCKED");
    x86os_putchar('\n');
    if (!list_line) {
        x86os_puts("ADMIN STATUS_STATE resource=");
        x86os_print_number((int)resource);
        x86os_puts(" state=");
        print_state_value(result.state);
        x86os_putchar('\n');
        x86os_puts("ADMIN STATUS_MOUNT resource=");
        x86os_print_number((int)resource);
        x86os_puts(" path=");
        x86os_puts((result.flags & X86OS_ADMIN_RESOURCE_MOUNTED) != 0U
            ? result.mount_path : "-");
        x86os_putchar('\n');
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && equal(argv[1], "list")) {
        for (uint32_t resource = 0U; resource < RESOURCE_LIMIT; ++resource) {
            int code = status(resource, 1);
            if (code == 0) continue;
            if (code != -22) {
                x86os_puts("ADMIN LIST_FAILED code=");
                x86os_print_number(code);
                x86os_putchar('\n');
                return 1;
            }
            break;
        }
        return 0;
    }
    if (argc != 3 ||
        (!equal(argv[1], "status") && !equal(argv[1], "down") &&
         !equal(argv[1], "up"))) {
        x86os_puts("Usage: devctl list\n");
        x86os_puts("       devctl status|down|up <resource>\n");
        return 2;
    }
    uint32_t resource = 0U;
    if (parse_resource(argv[2], &resource) != 0) return 2;
    if (equal(argv[1], "status")) {
        int code = status(resource, 0);
        if (code == 0) return 0;
        x86os_puts("ADMIN STATUS_FAILED code=");
        x86os_print_number(code);
        x86os_putchar('\n');
        return 1;
    }
    x86os_admin_storage_request_t request = {0};
    x86os_admin_storage_result_t result;
    request.version = X86OS_ADMIN_MAINTENANCE_VERSION;
    request.struct_size = sizeof(request);
    request.command = equal(argv[1], "down")
        ? X86OS_ADMIN_STORAGE_DEVICE_DOWN : X86OS_ADMIN_STORAGE_DEVICE_UP;
    request.resource = resource;
    request.drain_timeout_ms = 500U;
    int code = x86os_admin_storage(&request, &result);
    if (code != 0) {
        if (code == -1001) x86os_puts("ADMIN ROOT_PROTECTED\n");
        else {
            x86os_puts("ADMIN DEVICE_FAILED code=");
            x86os_print_number(code);
            x86os_putchar('\n');
        }
        return 1;
    }
    x86os_puts(request.command == X86OS_ADMIN_STORAGE_DEVICE_DOWN
        ? "ADMIN DEVICE_DOWN_OK resource="
        : "ADMIN DEVICE_UP_OK resource=");
    x86os_print_number((int)resource);
    x86os_putchar('\n');
    return 0;
}
