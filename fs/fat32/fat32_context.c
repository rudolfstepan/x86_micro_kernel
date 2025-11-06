#include "fat32_internal.h"

// ============================================================================
// Global FAT32 Context State
// ============================================================================

// Boot sector information for the currently mounted FAT32 filesystem
// Note: Must be properly aligned for DMA/PIO operations (512 bytes)
struct Fat32BootSector boot_sector __attribute__((aligned(512)));

// Current working directory cluster (default: root cluster = 2 for FAT32)
unsigned int current_directory_cluster = 2;

// ATA drive parameters for the currently mounted drive
unsigned short ata_base_address = 0x1F0;
bool ata_is_master = true;

// ============================================================================
// Context Management Functions
// ============================================================================

/**
 * @brief Initialize the FAT32 context with drive parameters
 * 
 * @param base ATA base I/O port address
 * @param is_master True if master drive, false if slave
 * @param root_cluster Root directory cluster number
 */
void fat32_set_context(unsigned short base, bool is_master, unsigned int root_cluster) {
    ata_base_address = base;
    ata_is_master = is_master;
    current_directory_cluster = root_cluster;
}

/**
 * @brief Get the current directory cluster
 * 
 * @return Current directory cluster number
 */
unsigned int fat32_get_current_directory(void) {
    return current_directory_cluster;
}

/**
 * @brief Set the current directory cluster
 * 
 * @param cluster New current directory cluster
 */
void fat32_set_current_directory(unsigned int cluster) {
    current_directory_cluster = cluster;
}
