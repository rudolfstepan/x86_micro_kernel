#!/usr/bin/env python3
"""
Unit tests for FAT32/FAT12 disk images
Tests image structure, boot sector, and file contents without booting kernel
"""

import sys
import struct
import subprocess
import os
import shutil
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, List, Tuple

# ANSI color codes for output
class Color:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

@dataclass
class FAT32BootSector:
    """FAT32 Boot Sector structure"""
    bytes_per_sector: int
    sectors_per_cluster: int
    reserved_sectors: int
    num_fats: int
    root_entries: int  # 0 for FAT32
    total_sectors_16: int  # 0 for FAT32
    total_sectors_32: int
    media_descriptor: int
    sectors_per_fat_16: int  # 0 for FAT32
    sectors_per_fat_32: int
    root_cluster: int
    fs_info_sector: int
    backup_boot_sector: int
    volume_label: str
    fs_type: str

    @property
    def total_sectors(self) -> int:
        return self.total_sectors_16 or self.total_sectors_32

    @property
    def declared_size(self) -> int:
        return self.total_sectors * self.bytes_per_sector

    def fits_image_size(self, image_size: int) -> bool:
        return self.declared_size > 0 and self.declared_size <= image_size

    @property
    def cluster_count(self) -> Optional[int]:
        """Derive the FAT type from BPB geometry, as required by the FAT spec."""
        if (
            self.bytes_per_sector not in [512, 1024, 2048, 4096]
            or self.sectors_per_cluster not in [1, 2, 4, 8, 16, 32, 64, 128]
            or self.reserved_sectors == 0
            or self.num_fats not in [1, 2]
        ):
            return None

        total_sectors = self.total_sectors
        sectors_per_fat = self.sectors_per_fat_16 or self.sectors_per_fat_32
        if total_sectors == 0 or sectors_per_fat == 0:
            return None

        root_dir_sectors = (
            self.root_entries * 32 + self.bytes_per_sector - 1
        ) // self.bytes_per_sector
        non_data_sectors = (
            self.reserved_sectors
            + self.num_fats * sectors_per_fat
            + root_dir_sectors
        )
        if total_sectors <= non_data_sectors:
            return None
        return (total_sectors - non_data_sectors) // self.sectors_per_cluster
    
    @property
    def is_valid_fat32(self) -> bool:
        """Check if this is a valid FAT32 boot sector"""
        clusters = self.cluster_count
        return (
            clusters is not None and
            clusters >= 65525 and
            (self.sectors_per_fat_32 * self.bytes_per_sector) // 4 >=
                clusters + 2 and
            self.root_entries == 0 and  # FAT32 specific
            self.total_sectors_16 == 0 and  # FAT32 specific
            self.sectors_per_fat_16 == 0 and  # FAT32 specific
            self.sectors_per_fat_32 > 0 and
            2 <= self.root_cluster < clusters + 2
        )
    
    @property
    def is_valid_fat12(self) -> bool:
        """Check if this is a valid FAT12 boot sector"""
        clusters = self.cluster_count
        return (
            clusters is not None and
            0 < clusters < 4085 and
            (self.sectors_per_fat_16 * self.bytes_per_sector * 8) // 12 >=
                clusters + 2 and
            self.root_entries > 0 and  # FAT12/16 specific
            self.sectors_per_fat_16 > 0
        )

class DiskImageTester:
    """Test harness for disk images"""
    
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.tests_passed = 0
        self.tests_failed = 0
        self.tests_skipped = 0
        
    def log(self, message: str, level: str = 'INFO'):
        """Log message with color coding"""
        if level == 'PASS':
            print(f"{Color.GREEN}[PASS]{Color.RESET} {message}")
            self.tests_passed += 1
        elif level == 'FAIL':
            print(f"{Color.RED}[FAIL]{Color.RESET} {message}")
            self.tests_failed += 1
        elif level == 'WARN':
            print(f"{Color.YELLOW}[WARN]{Color.RESET} {message}")
        elif level == 'SKIP':
            print(f"{Color.YELLOW}[SKIP]{Color.RESET} {message}")
            self.tests_skipped += 1
        elif level == 'INFO' and self.verbose:
            print(f"{Color.BLUE}[INFO]{Color.RESET} {message}")
    
    def read_boot_sector(self, image_path: Path) -> Optional[FAT32BootSector]:
        """Read and parse FAT32/FAT12 boot sector"""
        try:
            with open(image_path, 'rb') as f:
                data = f.read(512)

            if len(data) != 512:
                self.log(f"Boot sector in {image_path} is only {len(data)} bytes", 'FAIL')
                return None
                
            # Parse common BPB fields (FAT12/16/32)
            bytes_per_sector = struct.unpack('<H', data[11:13])[0]
            sectors_per_cluster = data[13]
            reserved_sectors = struct.unpack('<H', data[14:16])[0]
            num_fats = data[16]
            root_entries = struct.unpack('<H', data[17:19])[0]
            total_sectors_16 = struct.unpack('<H', data[19:21])[0]
            total_sectors_32 = struct.unpack('<I', data[32:36])[0]
            media_descriptor = data[21]
            sectors_per_fat_16 = struct.unpack('<H', data[22:24])[0]
            
            # Check if FAT32
            if sectors_per_fat_16 == 0:
                # FAT32 specific fields
                sectors_per_fat_32 = struct.unpack('<I', data[36:40])[0]
                root_cluster = struct.unpack('<I', data[44:48])[0]
                fs_info_sector = struct.unpack('<H', data[48:50])[0]
                backup_boot_sector = struct.unpack('<H', data[50:52])[0]
                volume_label = data[71:82].decode('ascii', errors='ignore').strip()
                fs_type = data[82:90].decode('ascii', errors='ignore').strip()
            else:
                # FAT12/16
                sectors_per_fat_32 = 0
                root_cluster = 0  # FAT12/16 use fixed root directory
                fs_info_sector = 0
                backup_boot_sector = 0
                volume_label = data[43:54].decode('ascii', errors='ignore').strip()
                fs_type = data[54:62].decode('ascii', errors='ignore').strip()
            
            boot_sector = FAT32BootSector(
                bytes_per_sector=bytes_per_sector,
                sectors_per_cluster=sectors_per_cluster,
                reserved_sectors=reserved_sectors,
                num_fats=num_fats,
                root_entries=root_entries,
                total_sectors_16=total_sectors_16,
                total_sectors_32=total_sectors_32,
                media_descriptor=media_descriptor,
                sectors_per_fat_16=sectors_per_fat_16,
                sectors_per_fat_32=sectors_per_fat_32,
                root_cluster=root_cluster,
                fs_info_sector=fs_info_sector,
                backup_boot_sector=backup_boot_sector,
                volume_label=volume_label,
                fs_type=fs_type
            )
            
            if self.verbose:
                self.log(f"Boot sector parsed: {boot_sector}", 'INFO')
            
            return boot_sector
            
        except Exception as e:
            self.log(f"Failed to read boot sector from {image_path}: {e}", 'FAIL')
            return None
    
    def test_image_exists(self, image_path: Path) -> bool:
        """Test if image file exists and is readable"""
        if not image_path.exists():
            self.log(f"Image not found: {image_path}", 'FAIL')
            return False
        
        if not image_path.is_file():
            self.log(f"Not a file: {image_path}", 'FAIL')
            return False
        
        if image_path.stat().st_size == 0:
            self.log(f"Image is empty: {image_path}", 'FAIL')
            return False
        
        self.log(f"Image exists: {image_path.name} ({image_path.stat().st_size} bytes)", 'PASS')
        return True
    
    def test_boot_sector(self, image_path: Path, expected_fs_type: str = 'FAT32') -> bool:
        """Test boot sector structure"""
        boot_sector = self.read_boot_sector(image_path)
        
        if not boot_sector:
            return False
        
        # Check filesystem type
        if expected_fs_type == 'FAT32':
            if not boot_sector.is_valid_fat32:
                self.log(f"Invalid FAT32 boot sector in {image_path.name}", 'FAIL')
                self.log(f"  Bytes/Sector: {boot_sector.bytes_per_sector}", 'INFO')
                self.log(f"  Sectors/Cluster: {boot_sector.sectors_per_cluster}", 'INFO')
                self.log(f"  Root Cluster: {boot_sector.root_cluster}", 'INFO')
                self.log(f"  FS Type: '{boot_sector.fs_type}'", 'INFO')
                return False
            self.log(f"Valid FAT32 boot sector: {image_path.name}", 'PASS')
        elif expected_fs_type == 'FAT12':
            if not boot_sector.is_valid_fat12:
                self.log(f"Invalid FAT12 boot sector in {image_path.name}", 'FAIL')
                return False
            self.log(f"Valid FAT12 boot sector: {image_path.name}", 'PASS')

        if not boot_sector.fits_image_size(image_path.stat().st_size):
            self.log(
                f"Declared filesystem size ({boot_sector.declared_size} bytes) "
                f"exceeds image size ({image_path.stat().st_size} bytes)",
                'FAIL',
            )
            return False
        
        # Additional checks
        if boot_sector.bytes_per_sector != 512:
            self.log(f"Non-standard sector size: {boot_sector.bytes_per_sector}", 'WARN')
        
        if boot_sector.num_fats != 2:
            self.log(f"Unusual FAT count: {boot_sector.num_fats}", 'WARN')
        
        return True
    
    def test_mount_and_files(self, image_path: Path, expected_files: List[str]) -> bool:
        """Test mounting image and checking for expected files"""
        mount_point = Path('/tmp/test_mount_kernel')

        if os.name != 'posix' or shutil.which('mount') is None or shutil.which('sudo') is None:
            self.log('Mount test requires a POSIX host with mount and sudo', 'SKIP')
            return True
        
        try:
            # Create mount point
            subprocess.run(['mkdir', '-p', str(mount_point)], check=True, capture_output=True)
            
            # Mount image
            result = subprocess.run(
                ['sudo', '-n', 'mount', '-o', 'loop', str(image_path), str(mount_point)],
                capture_output=True,
                text=True
            )
            
            if result.returncode != 0:
                self.log(f"Failed to mount {image_path.name}: {result.stderr}", 'FAIL')
                return False
            
            self.log(f"Successfully mounted: {image_path.name}", 'PASS')
            
            # Check for expected files/directories
            all_found = True
            for expected in expected_files:
                file_path = mount_point / expected
                if file_path.exists():
                    self.log(f"Found: {expected}", 'PASS')
                else:
                    self.log(f"Missing: {expected}", 'FAIL')
                    all_found = False
            
            # List all contents if verbose
            if self.verbose:
                result = subprocess.run(
                    ['find', str(mount_point), '-type', 'f'],
                    capture_output=True,
                    text=True
                )
                if result.stdout:
                    self.log(f"Files in {image_path.name}:", 'INFO')
                    for line in result.stdout.strip().split('\n'):
                        rel_path = line.replace(str(mount_point), '')
                        self.log(f"  {rel_path}", 'INFO')
            
            # Unmount
            subprocess.run(['sudo', '-n', 'umount', str(mount_point)], check=True, capture_output=True)
            
            return all_found
            
        except Exception as e:
            self.log(f"Mount test failed: {e}", 'FAIL')
            # Try to unmount if mounted
            subprocess.run(['sudo', '-n', 'umount', str(mount_point)], capture_output=True)
            return False
    
    def test_signature(self, image_path: Path) -> bool:
        """Test for boot sector signature (0x55AA)"""
        try:
            with open(image_path, 'rb') as f:
                f.seek(510)
                signature = struct.unpack('<H', f.read(2))[0]
            
            if signature == 0xAA55:
                self.log(f"Valid boot signature: {image_path.name} (0x55AA)", 'PASS')
                return True
            else:
                self.log(f"Invalid boot signature: {image_path.name} (0x{signature:04X}, expected 0xAA55)", 'FAIL')
                return False
                
        except Exception as e:
            self.log(f"Failed to check signature: {e}", 'FAIL')
            return False
    
    def run_all_tests(self, project_root: Path):
        """Run all disk image tests"""
        print(f"\n{Color.BOLD}=== Disk Image Unit Tests ==={Color.RESET}\n")
        
        # Define test cases
        test_cases = [
            {
                'path': project_root / 'disk.img',
                'fs_type': 'FAT32',
                'expected_files': ['README.TXT', 'TEST', 'DOCS', 'BIN', 'sys'],
                'description': 'Primary HDD (HDD0)'
            },
            {
                'path': project_root / 'disk1.img',
                'fs_type': 'FAT32',
                'expected_files': ['README.TXT', 'TEST', 'DOCS', 'BIN'],
                'description': 'Secondary HDD (HDD1)'
            },
            {
                'path': project_root / 'floppy.img',
                'fs_type': 'FAT12',
                'expected_files': ['README.TXT', 'TEST', 'DOCS', 'BIN'],
                'description': 'Floppy disk (FD0)'
            }
        ]
        
        for test_case in test_cases:
            image_path = test_case['path']
            print(f"\n{Color.BOLD}Testing: {test_case['description']}{Color.RESET}")
            print(f"Image: {image_path.name}")
            print("-" * 60)
            
            # Test 1: Image exists
            if not self.test_image_exists(image_path):
                continue
            
            # Test 2: Boot signature
            self.test_signature(image_path)
            
            # Test 3: Boot sector structure
            self.test_boot_sector(image_path, test_case['fs_type'])
            
            # Test 4: Mount and check files
            self.test_mount_and_files(image_path, test_case['expected_files'])
        
        # Print summary
        print(f"\n{Color.BOLD}=== Test Summary ==={Color.RESET}")
        total = self.tests_passed + self.tests_failed
        pass_rate = (self.tests_passed / total * 100) if total > 0 else 0
        
        print(f"Total tests: {total}")
        print(f"{Color.GREEN}Passed: {self.tests_passed}{Color.RESET}")
        print(f"{Color.RED}Failed: {self.tests_failed}{Color.RESET}")
        print(f"{Color.YELLOW}Skipped: {self.tests_skipped}{Color.RESET}")
        print(f"Pass rate: {pass_rate:.1f}%\n")
        
        return self.tests_failed == 0

def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Test disk images for REIST OS')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('--project-root', type=Path, default=Path.cwd(),
                       help='Project root directory (default: current directory)')
    
    args = parser.parse_args()
    
    tester = DiskImageTester(verbose=args.verbose)
    success = tester.run_all_tests(args.project_root)
    
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
