#ifndef DRIVES_H
#define DRIVES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DRIVE_TYPE_NONE = 0,
    DRIVE_TYPE_ATA = 1,
    DRIVE_TYPE_FDD = 2
} drive_type_t;

typedef struct {
    drive_type_t type;      // Type of drive: ATA or FDD
    uint16_t base;          // Base I/O port (for ATA drives)
    bool is_master;         // True if master (ATA), false if slave (ATA)
    char name[8];           // Drive name (e.g., "hdd1", "fdd1")
    char model[41];         // Drive model string (for ATA drives)
    uint32_t sectors;       // Total sectors (for ATA drives)
    unsigned int cylinder;  // Number of cylinders (for FDD)
    unsigned int head;      // Number of heads (for FDD)
    unsigned int sector;    // Number of sectors (for FDD)
} drive_t;

// global definition of the current drive which is being accessed
extern short drive_count;
extern drive_t* current_drive;
extern drive_t detected_drives[];  // Global array of detected drives


#endif