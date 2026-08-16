#include "x86os.h"

static void print_unsigned(uint32_t value) {
    char digits[10];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U && count < sizeof(digits));
    while (count != 0U) x86os_putchar(digits[--count]);
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc > 1) {
        x86os_puts("fdisk: partition mutation is unavailable in read-only mode\n");
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
        else { x86os_puts("UNKNOWN("); print_unsigned(drive.type);
               x86os_putchar(')'); }
        x86os_puts("  ");
        x86os_puts(drive.mount_point);
        x86os_putchar('\n');
    }
    return 0;
}
