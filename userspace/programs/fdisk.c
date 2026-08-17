#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

static int equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) ++left, ++right;
    return *left == *right;
}

static int parse_unsigned(const char *text, uint32_t *value) {
    uint32_t result = 0U;
    if (text == 0 || value == 0 || *text == '\0') return -1;
    while (*text != '\0') {
        uint32_t digit = (uint32_t)(*text - '0');
        if (digit > 9U || result > (UINT32_MAX - digit) / 10U) return -1;
        result = result * 10U + digit; ++text;
    }
    *value = result; return 0;
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc == 5 && equal(argv[1], "--create") &&
        equal(argv[4], "--confirm")) {
        uint32_t resource = 0U, type = 0U;
        if (parse_unsigned(argv[2], &resource) != 0 ||
            parse_unsigned(argv[3], &type) != 0 || type > 255U || type == 0U) {
            x86os_puts("fdisk: usage --create <resource> <type> --confirm\n");
            return 2;
        }
        x86os_drive_info_t drive;
        if (x86os_drive_info(resource, &drive) <= 0 || drive.sectors <= 2048U) {
            x86os_puts("fdisk: invalid or unavailable whole disk\n"); return 1;
        }
        x86os_partition_request_t request = {
            .version = X86OS_PARTITION_REQUEST_VERSION,
            .struct_size = sizeof(request), .resource = resource,
            .first_lba = 2048U,
            .sectors = (drive.sectors - 2048U) & ~2047U,
            .type = type, .confirm = 0x52454953U,
        };
        int result = x86os_partition_create(&request);
        if (result == 0) x86os_puts("fdisk: partition created\n");
        else { x86os_puts("fdisk: partition creation failed code=");
               print_unsigned((uint32_t)(-result)); x86os_putchar('\n'); }
        return result == 0 ? 0 : 1;
    }
    if (argc > 1) {
        x86os_puts("fdisk: usage --create <resource> <type> --confirm\n");
        return 2;
    }
    x86os_puts("Device  Type  Mount\n");
    for (uint32_t index = 0U;; ++index) {
        x86os_drive_info_t drive;
        int result = x86os_drive_info(index, &drive);
        if (result == 0) break;
        if (result < 0) continue;
        x86os_puts(drive.name);
        x86os_puts("  ");
        if (drive.type == X86OS_DRIVE_FDD) x86os_puts("FDD");
        else if (drive.type == X86OS_DRIVE_ATA) x86os_puts("ATA");
        else if (drive.type == X86OS_DRIVE_AHCI) x86os_puts("SATA");
        else if (drive.type == X86OS_DRIVE_PARTITION) x86os_puts("PART");
        else { x86os_puts("UNKNOWN("); print_unsigned(drive.type);
               x86os_putchar(')'); }
        x86os_puts("  ");
        x86os_puts(drive.mount_point);
        x86os_putchar('\n');
    }
    return 0;
}
