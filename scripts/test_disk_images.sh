#!/bin/bash
# Simple bash script to test disk images without booting kernel
# Tests: existence, boot signature, filesystem type, and file contents

# Note: Don't use 'set -e' because we want to continue even if tests fail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
log_pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}✗${NC} $1"
    ((TESTS_FAILED++))
}

log_warn() {
    echo -e "${YELLOW}⚠${NC} $1"
}

log_info() {
    echo -e "${BLUE}ℹ${NC} $1"
}

# Test if image exists and has content
test_image_exists() {
    local image=$1
    local name=$2
    
    if [ ! -f "$image" ]; then
        log_fail "$name: Image not found ($image)"
        return 1
    fi
    
    local size=$(stat -c%s "$image" 2>/dev/null || echo 0)
    if [ "$size" -eq 0 ]; then
        log_fail "$name: Image is empty"
        return 1
    fi
    
    log_pass "$name: Image exists ($(numfmt --to=iec-i --suffix=B $size))"
    return 0
}

# Test boot sector signature (0x55AA at offset 510)
test_boot_signature() {
    local image=$1
    local name=$2
    
    # Read bytes at offset 510-511 using dd instead of xxd
    local sig=$(dd if="$image" bs=1 skip=510 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')
    
    if [ "$sig" = "55aa" ]; then
        log_pass "$name: Valid boot signature (0x55AA)"
        return 0
    else
        log_fail "$name: Invalid boot signature (0x$sig, expected 0x55AA)"
        return 1
    fi
}

# Test filesystem type using file command
test_filesystem_type() {
    local image=$1
    local name=$2
    local expected=$3
    
    local fs_type=$(file -b "$image" 2>/dev/null)
    
    if echo "$fs_type" | grep -q "$expected"; then
        log_pass "$name: Correct filesystem type ($expected detected)"
        return 0
    else
        log_fail "$name: Wrong filesystem type (detected: $fs_type, expected: $expected)"
        return 1
    fi
}

# Test FAT32 boot sector structure
test_fat32_boot_sector() {
    local image=$1
    local name=$2
    
    # Read bytes per sector (offset 11, 2 bytes, little-endian) using dd
    local bps_raw=$(dd if="$image" bs=1 skip=11 count=2 2>/dev/null | od -An -tu2 -N2)
    local bps=$(echo $bps_raw | tr -d ' ')
    
    # Read sectors per cluster (offset 13, 1 byte)
    local spc_raw=$(dd if="$image" bs=1 skip=13 count=1 2>/dev/null | od -An -tu1)
    local spc=$(echo $spc_raw | tr -d ' ')
    
    # Read root cluster (offset 44, 4 bytes, little-endian)
    local root_cluster_raw=$(dd if="$image" bs=1 skip=44 count=4 2>/dev/null | od -An -tu4 -N4)
    local root_cluster=$(echo $root_cluster_raw | tr -d ' ')
    
    # Validate
    local valid=true
    
    if [ "$bps" != "512" ] && [ "$bps" != "" ]; then
        log_warn "$name: Non-standard bytes per sector: $bps"
    fi
    
    if [ "$spc" != "" ] && ([ "$spc" -lt 1 ] || [ "$spc" -gt 128 ]); then
        log_fail "$name: Invalid sectors per cluster: $spc"
        valid=false
    fi
    
    if [ "$root_cluster" != "" ] && [ "$root_cluster" -lt 2 ]; then
        log_fail "$name: Invalid root cluster: $root_cluster"
        valid=false
    fi
    
    if $valid; then
        log_pass "$name: Valid FAT32 boot sector (BPS=$bps, SPC=$spc, RootCluster=$root_cluster)"
        return 0
    else
        return 1
    fi
}

# Test mounting and file contents
test_mount_and_files() {
    local image=$1
    local name=$2
    shift 2
    local expected_files=("$@")
    
    local mount_point="/tmp/test_mount_kernel_$$"
    
    # Create mount point
    mkdir -p "$mount_point"
    
    # Try to mount
    if ! sudo mount -o loop "$image" "$mount_point" 2>/dev/null; then
        log_fail "$name: Failed to mount image"
        rmdir "$mount_point" 2>/dev/null
        return 1
    fi
    
    log_pass "$name: Successfully mounted"
    
    # Check for expected files
    local all_found=true
    for file in "${expected_files[@]}"; do
        if [ -e "$mount_point/$file" ]; then
            log_pass "$name: Found $file"
        else
            log_fail "$name: Missing $file"
            all_found=false
        fi
    done
    
    # Unmount
    sudo umount "$mount_point" 2>/dev/null
    rmdir "$mount_point" 2>/dev/null
    
    if $all_found; then
        return 0
    else
        return 1
    fi
}

# Main test suite
run_all_tests() {
    echo -e "\n${BOLD}=== Disk Image Unit Tests ===${NC}\n"
    
    # Test disk.img (FAT32, HDD0)
    echo -e "\n${BOLD}Testing: Primary HDD (HDD0)${NC}"
    echo "Image: disk.img"
    echo "------------------------------------------------------------"
    test_image_exists "disk.img" "disk.img"
    test_boot_signature "disk.img" "disk.img"
    test_filesystem_type "disk.img" "disk.img" "FAT"
    test_fat32_boot_sector "disk.img" "disk.img"
    test_mount_and_files "disk.img" "disk.img" "README.TXT" "TEST" "DOCS" "BIN" "sys"
    
    # Test disk1.img (FAT32, HDD1)
    echo -e "\n${BOLD}Testing: Secondary HDD (HDD1)${NC}"
    echo "Image: disk1.img"
    echo "------------------------------------------------------------"
    test_image_exists "disk1.img" "disk1.img"
    test_boot_signature "disk1.img" "disk1.img"
    test_filesystem_type "disk1.img" "disk1.img" "FAT"
    test_fat32_boot_sector "disk1.img" "disk1.img"
    test_mount_and_files "disk1.img" "disk1.img" "README.TXT" "TEST" "DOCS" "BIN"
    
    # Test floppy.img (FAT12, FD0)
    echo -e "\n${BOLD}Testing: Floppy Disk (FD0)${NC}"
    echo "Image: floppy.img"
    echo "------------------------------------------------------------"
    test_image_exists "floppy.img" "floppy.img"
    test_boot_signature "floppy.img" "floppy.img"
    test_filesystem_type "floppy.img" "floppy.img" "FAT"
    # Note: FAT12 has different boot sector structure, skip detailed validation
    test_mount_and_files "floppy.img" "floppy.img" "README.TXT" "TEST" "DOCS" "BIN"
    
    # Print summary
    echo -e "\n${BOLD}=== Test Summary ===${NC}"
    local total=$((TESTS_PASSED + TESTS_FAILED))
    local pass_rate=0
    if [ "$total" -gt 0 ]; then
        pass_rate=$((TESTS_PASSED * 100 / total))
    fi
    
    echo "Total tests: $total"
    echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
    echo -e "${RED}Failed: $TESTS_FAILED${NC}"
    echo -e "Pass rate: ${pass_rate}%\n"
    
    if [ "$TESTS_FAILED" -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

# Check if running from project root
if [ ! -f "Makefile" ]; then
    echo "Error: Must be run from project root directory"
    exit 1
fi

# Check dependencies - only basic POSIX tools needed
if ! command -v dd &> /dev/null; then
    echo "Error: dd command not found"
    exit 1
fi

if ! command -v od &> /dev/null; then
    echo "Error: od command not found"
    exit 1
fi

# Run tests
run_all_tests
exit $?
