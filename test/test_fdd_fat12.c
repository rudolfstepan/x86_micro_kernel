/**
 * @file test/test_fdd_fat12.c
 * @brief Hostseitiger Regressionstest für fdd fat12.c.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "../drivers/block/fdd.h"
#include "../fs/fat12/fat12.h"
#include "../lib/libc/stdio.h"
#include "../lib/libc/stdlib.h"

int main() {
    printf("Starting FDD and FAT12 test...\n");

    // Initialize FDD controller
    if (!fdc_init_controller()) {
        printf("FDD controller initialization failed.\n");
        return 1;
    }
    printf("FDD controller initialized successfully.\n");

    // Detect and calibrate drives
    fdd_detect_drives();

    // Initialize FAT12 filesystem on drive 0
    if (!fat12_init_fs(0)) {
        printf("FAT12 initialization failed.\n");
        return 1;
    }
    printf("FAT12 filesystem initialized successfully.\n");

    // Read root directory
    if (!fat12_read_dir(NULL)) {
        printf("Failed to read root directory.\n");
        return 1;
    }
    printf("Root directory read successfully.\n");

    printf("FDD and FAT12 test completed.\n");
    return 0;
}
