#!/bin/bash
# Script to rename C naming conventions in the x86 microkernel

cd /home/rudolf/repos/x86_micro_kernel

echo "Renaming FAT32 structures..."

# Rename FAT32 structs
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/struct Fat32BootSector/struct fat32_boot_sector/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/struct FAT32DirEntry/struct fat32_dir_entry/g' {} \;

echo "Renaming FAT32 struct members..."

# Rename struct members for fat32_boot_sector
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.jumpBoot/\.jump_boot/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.OEMName/\.oem_name/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.bytesPerSector/\.bytes_per_sector/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.sectorsPerCluster/\.sectors_per_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.reservedSectorCount/\.reserved_sector_count/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.numberOfFATs/\.number_of_fats/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.rootEntryCount/\.root_entry_count/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.totalSectors16/\.total_sectors_16/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.mediaType/\.media_type/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.FATSize16/\.fat_size_16/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.sectorsPerTrack/\.sectors_per_track/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.numberOfHeads/\.number_of_heads/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.hiddenSectors/\.hidden_sectors/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.totalSectors32/\.total_sectors_32/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.FATSize32/\.fat_size_32/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.rootCluster/\.root_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.FSInfo/\.fs_info/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.backupBootSector/\.backup_boot_sector/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.driveNumber/\.drive_number/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.bootSignature/\.boot_signature/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.volumeID/\.volume_id/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.volumeLabel/\.volume_label/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.fileSystemType/\.file_system_type/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.bootCode/\.boot_code/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.bootSectorSignature/\.boot_sector_signature/g' {} \;

# Rename struct members for fat32_dir_entry
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.ntRes/\.nt_res/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.crtTimeTenth/\.crt_time_tenth/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.crtTime/\.crt_time/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.crtDate/\.crt_date/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.lastAccessDate/\.last_access_date/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.firstClusterHigh/\.first_cluster_high/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.writeTime/\.write_time/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.writeDate/\.write_date/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.firstClusterLow/\.first_cluster_low/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\.fileSize/\.file_size/g' {} \;

echo "Renaming camelCase function names to snake_case..."

# Rename functions
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bfindFileInDirectory\b/find_file_in_directory/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\breadFileData\b/read_file_data/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\breadFileDataToAddress\b/read_file_data_to_address/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bformatFilename\b/format_filename/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bisEndOfClusterChain\b/is_end_of_cluster_chain/g' {} \;

# Rename variables  
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bclusterNumber\b/cluster_number/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bstartCluster\b/start_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bcurrentCluster\b/current_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bnewCluster\b/new_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bparentCluster\b/parent_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bnewDirCluster\b/new_dir_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bfatSector\b/fat_sector/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\btargetCluster\b/target_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bdirName\b/dir_name/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bloadAddress\b/load_address/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bbytesToRead\b/bytes_to_read/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bbytesRead\b/bytes_read/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\btotalBytesRead\b/total_bytes_read/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bsectorNumber\b/sector_number/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bentriesPerCluster\b/entries_per_cluster/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bnewEntry\b/new_entry/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bfileSize\b/file_size/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bbufferPtr\b/buffer_ptr/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bbytesRemaining\b/bytes_remaining/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bbytesToReadNow\b/bytes_to_read_now/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bentryAdded\b/entry_added/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bdirEntries\b/dir_entries/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\bfoundEntry\b/found_entry/g' {} \;
find . -type f \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/\btempPath\b/temp_path/g' {} \;

echo "Done! All identifiers renamed to snake_case."
