# Disk Image Testing

This directory contains unit tests for validating disk images without booting the kernel.

## Test Scripts

### Python Version (Recommended)
```bash
make test              # Run all disk image tests
make test-verbose      # Run with detailed output
python3 scripts/test_disk_images.py -v
```

**Features:**
- Comprehensive boot sector validation (FAT32/FAT12)
- Detailed structure parsing (bytes/sector, sectors/cluster, root cluster)
- File system mounting and content verification
- Color-coded output with pass/fail indicators
- Detailed error reporting

**Requirements:**
- Python 3.6+
- `sudo` access for mounting

### Bash Version (No Dependencies)
```bash
make test-bash         # Run tests using only POSIX tools
bash scripts/test_disk_images.sh
```

**Features:**
- Uses only standard POSIX tools (dd, od, file, mount)
- No external dependencies (no Python, no xxd)
- Same validation as Python version
- Ideal for CI/CD environments

**Requirements:**
- `dd`, `od`, `file`, `mount` commands
- `sudo` access for mounting

### Quick Check
```bash
make test-quick        # Just check if images exist
```

Simple existence check - no mounting, no validation.

## What Gets Tested

Each disk image is tested for:

1. **Existence and Size**
   - File exists
   - Non-zero size
   - Correct disk size (50MB for HDD, 1.44MB for floppy)

2. **Boot Sector Signature**
   - Valid 0x55AA signature at offset 510

3. **Filesystem Type**
   - FAT32 for disk.img and disk1.img
   - FAT12 for floppy.img

4. **Boot Sector Structure** (FAT32)
   - Bytes per sector (512)
   - Sectors per cluster (valid range 1-128)
   - Root cluster (>= 2)
   - FAT32 identifier string

5. **File System Mounting**
   - Image can be mounted via loop device
   - Root directory is accessible

6. **Expected Files**
   - `README.TXT` exists
   - `TEST/` directory exists
   - `DOCS/` directory exists
   - `BIN/` directory exists
   - Additional directories as expected per image

## Test Images

| Image | Type | Size | Mount Point (Kernel) | Expected Files |
|-------|------|------|---------------------|----------------|
| `disk.img` | FAT32 | 50MB | HDD0 (Primary Master) | README.TXT, TEST/, DOCS/, BIN/, sys/ |
| `disk1.img` | FAT32 | 50MB | HDD1 (Primary Slave) | README.TXT, TEST/, DOCS/, BIN/ |
| `floppy.img` | FAT12 | 1.44MB | FD0 (Floppy) | README.TXT, TEST/, DOCS/, BIN/ |

## Exit Codes

- **0**: All tests passed
- **1**: One or more tests failed

## Example Output

```
=== Disk Image Unit Tests ===

Testing: Primary HDD (HDD0)
Image: disk.img
------------------------------------------------------------
✓ Image exists: disk.img (52428800 bytes)
✓ Valid boot signature: disk.img (0x55AA)
✓ Valid FAT32 boot sector: disk.img
✓ Successfully mounted: disk.img
✓ Found: README.TXT
✓ Found: TEST
✓ Found: DOCS
✓ Found: BIN
✓ Found: sys

=== Test Summary ===
Total tests: 25
Passed: 25
Failed: 0
Pass rate: 100.0%
```

## Troubleshooting

### Permission Denied
Tests require `sudo` for mounting. Run:
```bash
sudo python3 scripts/test_disk_images.py
# or
sudo bash scripts/test_disk_images.sh
```

### Images Not Found
Ensure you're in the project root and images exist:
```bash
ls -lh disk.img disk1.img floppy.img
```

If missing, create them:
```bash
bash scripts/format_disks.sh
sudo bash scripts/populate_disks.sh
```

### Invalid Boot Sector
If boot sector validation fails, the image may be corrupted. Recreate:
```bash
rm disk.img disk1.img floppy.img
bash scripts/format_disks.sh
sudo bash scripts/populate_disks.sh
```

### Mount Fails
Check if mount point is already in use:
```bash
mount | grep /tmp/test_mount
sudo umount /tmp/test_mount_kernel  # if needed
```

## Integration with Build System

The test targets are integrated into the Makefile:

```makefile
.PHONY: test test-images test-verbose test-bash test-quick

test: test-images
test-images:
    @python3 scripts/test_disk_images.py

test-verbose:
    @python3 scripts/test_disk_images.py -v

test-bash:
    @bash scripts/test_disk_images.sh

test-quick:
    @for img in disk.img disk1.img floppy.img; do \
        if [ -f "$$img" ]; then \
            echo "✓ $$img exists ($$(du -h $$img | cut -f1))"; \
        else \
            echo "✗ $$img missing"; \
        fi; \
    done
```

## CI/CD Usage

For continuous integration, use the bash version:

```yaml
# .github/workflows/test.yml example
test:
  runs-on: ubuntu-latest
  steps:
    - name: Checkout
      uses: actions/checkout@v2
    
    - name: Format disk images
      run: bash scripts/format_disks.sh
    
    - name: Populate disk images
      run: sudo bash scripts/populate_disks.sh
    
    - name: Run disk image tests
      run: sudo bash scripts/test_disk_images.sh
```

## Development Workflow

1. **After modifying disk images:**
   ```bash
   make test-quick    # Quick sanity check
   ```

2. **Before committing:**
   ```bash
   make test          # Full validation
   ```

3. **When debugging filesystem issues:**
   ```bash
   make test-verbose  # See detailed boot sector info
   ```

4. **In automated builds:**
   ```bash
   make test-bash     # No Python dependency
   ```
