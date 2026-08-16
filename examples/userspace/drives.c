#include "x86os.h"

static char drive_letter(const x86os_drive_info_t* drive) {
    if (drive->name[3] < '0' || drive->name[3] > '9') return '?';
    if (drive->type == X86OS_DRIVE_FDD) return (char)('A' + drive->name[3] - '0');
    if (drive->type == X86OS_DRIVE_ATA || drive->type == X86OS_DRIVE_AHCI)
        return (char)('C' + drive->name[3] - '0');
    return '?';
}

int main(void) {
    x86os_puts("Resource  Drive  Device  Type\n");
    x86os_puts("-----------------------------\n");
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
        else x86os_puts("HDD");
        x86os_putchar('\n');
    }
    return 0;
}
