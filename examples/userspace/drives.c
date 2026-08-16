#include "x86os.h"

static char drive_letter(const x86os_drive_info_t* drive) {
    if (drive->mount_point[0] == '/' && drive->mount_point[1] == '\0')
        return 'C';
    if (drive->name[3] < '0' || drive->name[3] > '9') return '?';
    if (drive->type == X86OS_DRIVE_FDD) return (char)('A' + drive->name[3] - '0');
    if (drive->type == X86OS_DRIVE_ATA || drive->type == X86OS_DRIVE_AHCI ||
        drive->type == X86OS_DRIVE_PARTITION)
        return (char)('C' + drive->name[3] - '0');
    return '?';
}

int main(void) {
    x86os_puts("Resource  Drive  Device  Type  Status\n");
    x86os_puts("-------------------------------------\n");
    for (uint32_t index = 0;; ++index) {
        x86os_drive_info_t drive;
        int result = x86os_drive_info(index, &drive);
        if (result == 0) break;
        if (result < 0) continue;
        x86os_print_number((int)index);
        x86os_puts("        ");
        x86os_putchar(drive_letter(&drive));
        x86os_puts(":     ");
        x86os_puts(drive.name);
        x86os_puts("    ");
        if (drive.type == X86OS_DRIVE_FDD) x86os_puts("FDD");
        else if (drive.type == X86OS_DRIVE_AHCI) x86os_puts("SATA");
        else if (drive.type == X86OS_DRIVE_PARTITION) x86os_puts("PART");
        else x86os_puts("HDD");
        x86os_puts("   ");
        x86os_drive_status_t status;
        if (x86os_drive_status(index, &status) != 0) {
            x86os_puts("UNKNOWN");
        } else if ((status.flags & X86OS_DRIVE_STATUS_RECOVERING) != 0U) {
            x86os_puts("RECOVERING");
        } else if ((status.flags & X86OS_DRIVE_STATUS_QUARANTINED) != 0U) {
            x86os_puts("QUARANTINED");
        } else if ((status.flags & X86OS_DRIVE_STATUS_DEGRADED) != 0U) {
            x86os_puts("DEGRADED");
        } else if ((status.flags & X86OS_DRIVE_STATUS_READ_ONLY) != 0U) {
            x86os_puts("READONLY");
        } else if ((status.flags & X86OS_DRIVE_STATUS_AVAILABLE) != 0U) {
            x86os_puts("READY");
        } else {
            x86os_puts("OFFLINE");
        }
        x86os_putchar('\n');
    }
    return 0;
}
