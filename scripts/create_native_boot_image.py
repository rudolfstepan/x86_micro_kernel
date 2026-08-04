#!/usr/bin/env python3
"""Create a native BIOS disk image for QEMU, VMware, and raw media."""

from __future__ import annotations

import argparse
import binascii
import shutil
import struct
from collections.abc import Mapping
from pathlib import Path


SECTOR_SIZE = 512
IMAGE_SIZE = 64 * 1024 * 1024
PARTITION_START = 2048
DATA_PARTITION_START = 8192
STAGE2_RELATIVE_LBA = 1
STAGE2_MAX_SECTORS = 64
KERNEL_RELATIVE_LBA = 128
PARTITION_TYPE = 0xDA  # Non-filesystem raw boot partition
DATA_PARTITION_TYPE = 0x0C  # FAT32 with LBA addressing
MANIFEST_MAGIC = b"X86BOOT1"
MANIFEST_VERSION = 1
MAX_LOAD_ADDRESS = 0x04000000
VMWARE_BASENAME = "x86-microkernel"


def sectors_for(size: int) -> int:
    return (size + SECTOR_SIZE - 1) // SECTOR_SIZE


def validate_elf32(kernel: bytes) -> tuple[int, int]:
    # Stage 2 deliberately reads the first eight sectors in one operation so
    # the complete program-header table is available before segment loading.
    if len(kernel) < 4096:
        raise ValueError("kernel must occupy at least the 4 KiB ELF header window")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", kernel)
    ident, elf_type, machine, version, entry, phoff, _, _, ehsize, phentsize, phnum, _, _, _ = header
    if ident[:4] != b"\x7fELF" or ident[4:7] != b"\x01\x01\x01":
        raise ValueError("kernel is not a little-endian ELF32 image")
    if elf_type != 2 or machine != 3 or version != 1 or ehsize != 52:
        raise ValueError("kernel is not an i386 ET_EXEC image")
    if phentsize != 32 or not (1 <= phnum <= 16):
        raise ValueError("unsupported ELF32 program-header table")
    phend = phoff + phnum * phentsize
    if phoff < ehsize or phend > min(len(kernel), 4096):
        raise ValueError("ELF32 program headers must fit in the first 4 KiB")

    load_count = 0
    entry_is_executable = False
    for index in range(phnum):
        values = struct.unpack_from("<IIIIIIII", kernel, phoff + index * phentsize)
        p_type, offset, vaddr, paddr, filesz, memsz, flags, _ = values
        if p_type != 1:
            continue
        load_count += 1
        if filesz > memsz or offset + filesz > len(kernel):
            raise ValueError(f"invalid PT_LOAD #{index} file extent")
        if vaddr != paddr:
            raise ValueError(f"PT_LOAD #{index} is not identity-addressed")
        if paddr < 0x00100000 or paddr + memsz > MAX_LOAD_ADDRESS:
            raise ValueError(f"PT_LOAD #{index} lies outside the native loader range")
        if flags & 1 and paddr <= entry < paddr + memsz:
            entry_is_executable = True
    if load_count == 0 or not entry_is_executable:
        raise ValueError("ELF32 entry point is not in an executable PT_LOAD segment")
    return entry, load_count


def create_manifest(stage2_sectors: int, kernel: bytes,
                    partition_sectors: int) -> bytes:
    manifest = bytearray(SECTOR_SIZE)
    struct.pack_into(
        "<8sIIIIIIIIII",
        manifest,
        0,
        MANIFEST_MAGIC,
        MANIFEST_VERSION,
        48,
        STAGE2_RELATIVE_LBA,
        stage2_sectors,
        KERNEL_RELATIVE_LBA,
        len(kernel),
        partition_sectors,
        binascii.crc32(kernel) & 0xFFFFFFFF,
        0,
        0,
    )
    words = struct.unpack("<128I", manifest)
    checksum = (-sum(words)) & 0xFFFFFFFF
    struct.pack_into("<I", manifest, 44, checksum)
    if sum(struct.unpack("<128I", manifest)) & 0xFFFFFFFF:
        raise AssertionError("manifest checksum construction failed")
    return bytes(manifest)


def encode_chs(lba: int, heads: int = 16, sectors_per_track: int = 63) -> bytes:
    cylinder, remainder = divmod(lba, heads * sectors_per_track)
    head, sector_index = divmod(remainder, sectors_per_track)
    if cylinder > 1023 or head > 254:
        return b"\xFE\xFF\xFF"
    sector = sector_index + 1
    return bytes((head, sector | ((cylinder >> 2) & 0xC0), cylinder & 0xFF))


def patch_partition_table(stage1: bytes, partition_sectors: int,
                          total_sectors: int) -> bytes:
    if len(stage1) != SECTOR_SIZE or stage1[510:512] != b"\x55\xAA":
        raise ValueError("stage 1 must be exactly 512 bytes with a 0x55AA signature")
    if PARTITION_START + partition_sectors > DATA_PARTITION_START:
        raise ValueError("raw boot partition overlaps the FAT32 data partition")
    if DATA_PARTITION_START >= total_sectors:
        raise ValueError("disk is too small for the FAT32 data partition")
    patched = bytearray(stage1)
    patched[446:510] = bytes(64)
    boot_entry = struct.pack(
        "<B3sB3sII",
        0x80,
        encode_chs(PARTITION_START),
        PARTITION_TYPE,
        encode_chs(PARTITION_START + partition_sectors - 1),
        PARTITION_START,
        partition_sectors,
    )
    data_sectors = total_sectors - DATA_PARTITION_START
    data_entry = struct.pack(
        "<B3sB3sII",
        0x00,
        encode_chs(DATA_PARTITION_START),
        DATA_PARTITION_TYPE,
        b"\xFE\xFF\xFF",
        DATA_PARTITION_START,
        data_sectors,
    )
    patched[446:462] = boot_entry
    patched[462:478] = data_entry
    return bytes(patched)


def fat32_geometry(total_sectors: int) -> tuple[int, int]:
    reserved_sectors = 32
    fat_count = 2
    sectors_per_cluster = 1
    fat_sectors = 1
    for _ in range(32):
        data_sectors = total_sectors - reserved_sectors - fat_count * fat_sectors
        if data_sectors <= 0:
            raise ValueError("FAT32 partition is too small")
        cluster_count = data_sectors // sectors_per_cluster
        required = sectors_for((cluster_count + 2) * 4)
        if required == fat_sectors:
            return fat_sectors, cluster_count
        fat_sectors = required
    raise ValueError("FAT32 geometry did not converge")


def fat32_short_name(name: str) -> bytes:
    """Return an upper-case on-disk 8.3 name or reject the input."""
    if (
        not isinstance(name, str)
        or not name
        or not name.isascii()
        or "/" in name
        or "\\" in name
    ):
        raise ValueError(f"invalid FAT32 8.3 filename: {name!r}")
    parts = name.split(".")
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8:
        raise ValueError(f"invalid FAT32 8.3 filename: {name!r}")
    extension = parts[1] if len(parts) == 2 else ""
    if (len(parts) == 2 and not extension) or len(extension) > 3:
        raise ValueError(f"invalid FAT32 8.3 filename: {name!r}")

    allowed_punctuation = "!#$%&'()-@^_`{}~"
    normalized_parts = []
    for part in (parts[0], extension):
        try:
            encoded = part.upper().encode("ascii")
        except UnicodeEncodeError as error:
            raise ValueError(
                f"invalid FAT32 8.3 filename: {name!r}"
            ) from error
        if any(
            not chr(character).isalnum()
            and chr(character) not in allowed_punctuation
            for character in encoded
        ):
            raise ValueError(f"invalid FAT32 8.3 filename: {name!r}")
        normalized_parts.append(encoded)
    return normalized_parts[0].ljust(8, b" ") + normalized_parts[1].ljust(3, b" ")


def write_fat32_volume(image, partition_lba: int, total_sectors: int,
                       volume_id: int,
                       data_files: Mapping[str, bytes] | None = None) -> None:
    """Write a usable FAT32 volume containing README and optional 8.3 files."""
    reserved_sectors = 32
    fat_count = 2
    sectors_per_cluster = 1
    root_cluster = 2
    volume_label = b"X86 SYSTEM "
    fat_sectors, volume_cluster_count = fat32_geometry(total_sectors)
    first_data_sector = reserved_sectors + fat_count * fat_sectors
    cluster_size = sectors_per_cluster * SECTOR_SIZE

    readme = (
        b"x86 Microkernel - VMware data volume\r\n"
        b"This FAT32 partition is ready for shell file operations.\r\n"
    )
    files: list[tuple[bytes, bytes]] = [
        (fat32_short_name("README.TXT"), readme),
    ]
    seen_names = {files[0][0]}
    if data_files:
        for name, contents in data_files.items():
            short_name = fat32_short_name(name)
            if short_name in seen_names:
                raise ValueError(f"duplicate FAT32 filename: {name!r}")
            if not isinstance(contents, (bytes, bytearray, memoryview)):
                raise TypeError(f"contents for {name!r} must be bytes-like")
            contents = bytes(contents)
            if len(contents) > 0xFFFFFFFF:
                raise ValueError(f"file is too large for FAT32: {name!r}")
            seen_names.add(short_name)
            files.append((short_name, contents))

    # Reserve one entry for the volume label and one end marker.  Growing the
    # root chain keeps repeated --data-file options from being limited to the
    # first 16 directory slots.
    root_entry_bytes = (len(files) + 2) * 32
    root_cluster_count = max(1, sectors_for(root_entry_bytes))
    next_cluster = root_cluster
    root_clusters = list(range(next_cluster, next_cluster + root_cluster_count))
    next_cluster += root_cluster_count

    file_allocations: list[tuple[bytes, bytes, list[int]]] = []
    for short_name, contents in files:
        file_cluster_count = sectors_for(len(contents))
        clusters = list(range(next_cluster, next_cluster + file_cluster_count))
        next_cluster += file_cluster_count
        file_allocations.append((short_name, contents, clusters))

    allocated_cluster_count = next_cluster - 2
    if allocated_cluster_count > volume_cluster_count:
        raise ValueError("FAT32 data files do not fit in the partition")

    def write_relative_sector(relative_lba: int, data: bytes) -> None:
        if len(data) != SECTOR_SIZE:
            raise ValueError("FAT32 metadata writes must be exactly one sector")
        image.seek((partition_lba + relative_lba) * SECTOR_SIZE)
        image.write(data)

    def write_cluster(cluster: int, data: bytes) -> None:
        if len(data) != cluster_size:
            raise ValueError("FAT32 cluster writes must fill one cluster")
        first_sector = first_data_sector + (cluster - 2) * sectors_per_cluster
        for sector_index in range(sectors_per_cluster):
            offset = sector_index * SECTOR_SIZE
            write_relative_sector(
                first_sector + sector_index,
                data[offset:offset + SECTOR_SIZE],
            )

    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"X86MICRO"
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = sectors_per_cluster
    struct.pack_into("<H", boot, 14, reserved_sectors)
    boot[16] = fat_count
    struct.pack_into("<H", boot, 17, 0)
    struct.pack_into("<H", boot, 19, 0)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, 0)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 16)
    struct.pack_into("<I", boot, 28, partition_lba)
    struct.pack_into("<I", boot, 32, total_sectors)
    struct.pack_into("<I", boot, 36, fat_sectors)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, root_cluster)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, volume_id)
    boot[71:82] = volume_label
    boot[82:90] = b"FAT32   "
    boot[90:94] = b"\xFA\xF4\xEB\xFD"
    boot[510:512] = b"\x55\xAA"

    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    free_cluster_count = volume_cluster_count - allocated_cluster_count
    next_free_cluster = next_cluster if free_cluster_count else 0xFFFFFFFF
    struct.pack_into("<I", fsinfo, 488, free_cluster_count)
    struct.pack_into("<I", fsinfo, 492, next_free_cluster)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    write_relative_sector(0, bytes(boot))
    write_relative_sector(1, bytes(fsinfo))
    write_relative_sector(6, bytes(boot))
    write_relative_sector(7, bytes(fsinfo))

    fat = bytearray(fat_sectors * SECTOR_SIZE)
    struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
    struct.pack_into("<I", fat, 4, 0xFFFFFFFF)

    def link_cluster_chain(clusters: list[int]) -> None:
        for index, cluster in enumerate(clusters):
            successor = (
                clusters[index + 1]
                if index + 1 < len(clusters)
                else 0x0FFFFFFF
            )
            struct.pack_into("<I", fat, cluster * 4, successor)

    link_cluster_chain(root_clusters)
    for _, _, clusters in file_allocations:
        link_cluster_chain(clusters)
    for fat_index in range(fat_count):
        image.seek(
            (partition_lba + reserved_sectors + fat_index * fat_sectors)
            * SECTOR_SIZE
        )
        image.write(fat)

    root = bytearray(root_cluster_count * cluster_size)
    root[0:11] = volume_label
    root[11] = 0x08
    for entry_index, (short_name, contents, clusters) in enumerate(
        file_allocations, start=1
    ):
        entry_offset = entry_index * 32
        root[entry_offset:entry_offset + 11] = short_name
        root[entry_offset + 11] = 0x20
        start_cluster = clusters[0] if clusters else 0
        struct.pack_into("<H", root, entry_offset + 20, start_cluster >> 16)
        struct.pack_into("<H", root, entry_offset + 26, start_cluster & 0xFFFF)
        struct.pack_into("<I", root, entry_offset + 28, len(contents))

    for index, cluster in enumerate(root_clusters):
        offset = index * cluster_size
        write_cluster(cluster, bytes(root[offset:offset + cluster_size]))

    for _, contents, clusters in file_allocations:
        for index, cluster in enumerate(clusters):
            offset = index * cluster_size
            chunk = contents[offset:offset + cluster_size]
            write_cluster(cluster, chunk.ljust(cluster_size, b"\0"))


def write_vmdk_descriptor(path: Path, raw_image: Path, total_sectors: int,
                          content_id: int) -> None:
    # VMware normalizes this small IDE disk to complete 16x63 cylinders.  Keep
    # the descriptor geometry consistent with that firmware-visible value;
    # the RW extent still exposes every sector, including the partial tail.
    cylinders = max(1, total_sectors // (16 * 63))
    descriptor = f'''# Disk DescriptorFile
version=1
encoding="UTF-8"
CID={content_id:08x}
parentCID=ffffffff
createType="monolithicFlat"

# Extent description
RW {total_sectors} FLAT "{raw_image.name}" 0

# Disk Data Base
ddb.adapterType = "ide"
ddb.geometry.cylinders = "{cylinders}"
ddb.geometry.heads = "16"
ddb.geometry.sectors = "63"
ddb.virtualHWVersion = "4"
'''
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(descriptor, encoding="ascii", newline="\n")


def write_vmx(path: Path, vmdk_path: Path) -> None:
    configuration = f'''.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "20"
productCompatibility = "hosted"
displayName = "x86 Microkernel (Native BIOS)"
annotation = "x86 microkernel with its own BIOS MBR and ELF32 loader"
guestOS = "other"
firmware = "bios"
bios.bootOrder = "hdd"
bios.hddOrder = "ide0:0"
memsize = "512"
numvcpus = "1"
cpuid.coresPerSocket = "1"
mem.hotadd = "FALSE"
vcpu.hotadd = "FALSE"
ide0:0.present = "TRUE"
ide0:0.fileName = "{vmdk_path.name}"
ide0:0.deviceType = "disk"
ide0:0.mode = "persistent"
ide0:0.startConnected = "TRUE"
ide0:1.present = "FALSE"
ide1:0.present = "FALSE"
floppy0.present = "FALSE"
sound.present = "FALSE"
usb.present = "FALSE"
ehci.present = "FALSE"
xhci.present = "FALSE"
svga.present = "TRUE"
svga.autodetect = "TRUE"
mks.enable3d = "FALSE"
gui.fullScreenAtPowerOn = "FALSE"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "vmware-serial.log"
serial0.startConnected = "TRUE"
serial0.tryNoRxLoss = "TRUE"
serial0.yieldOnMsrRead = "TRUE"
ethernet0.present = "TRUE"
ethernet0.startConnected = "TRUE"
ethernet0.connectionType = "custom"
ethernet0.vnet = "VMnet0"
ethernet0.virtualDev = "e1000"
ethernet0.addressType = "generated"
ethernet0.wakeOnPcktRcv = "FALSE"
tools.syncTime = "FALSE"
tools.remindInstall = "FALSE"
tools.upgrade.policy = "manual"
'''
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(configuration, encoding="ascii", newline="\n")


def write_vmware_package(vm_directory: Path, raw_image: Path,
                         total_sectors: int, content_id: int) -> Path:
    vm_directory.mkdir(parents=True, exist_ok=True)
    flat_extent = vm_directory / f"{VMWARE_BASENAME}-flat.vmdk"
    descriptor = vm_directory / f"{VMWARE_BASENAME}.vmdk"
    vmx = vm_directory / f"{VMWARE_BASENAME}.vmx"
    shutil.copyfile(raw_image, flat_extent)
    write_vmdk_descriptor(descriptor, flat_extent, total_sectors, content_id)
    write_vmx(vmx, descriptor)

    launcher = r'''@echo off
setlocal
set "VMRUN=%ProgramFiles%\VMware\VMware Workstation\vmrun.exe"
if not exist "%VMRUN%" set "VMRUN=%ProgramFiles(x86)%\VMware\VMware Workstation\vmrun.exe"
if not exist "%VMRUN%" (
  echo VMware Workstation with vmrun.exe was not found.
  echo Open x86-microkernel.vmx manually after installing VMware Workstation.
  pause
  exit /b 1
)
"%VMRUN%" -T ws start "%~dp0x86-microkernel.vmx" gui
if errorlevel 1 (
  echo VMware could not start the virtual machine.
  pause
  exit /b 1
)
endlocal
'''
    (vm_directory / "START-VMWARE.cmd").write_text(
        launcher, encoding="ascii", newline="\r\n"
    )
    readme = """x86 Microkernel - fertige VMware-VM

Start:
  1. START-VMWARE.cmd doppelklicken, oder
  2. x86-microkernel.vmx in VMware Workstation öffnen und auf Play klicken.

Die VM ist bereits vollständig konfiguriert:
  - Legacy BIOS, Boot von der ersten IDE-Festplatte
  - 1 virtuelle CPU, 512 MiB RAM
  - VGA ohne 3D-Beschleunigung
  - Intel E1000, ueber VMnet0 direkt mit dem physischen LAN gebridged
  - automatische IPv4-Konfiguration per DHCP
  - 60-MiB-FAT32-Datenpartition mit README.TXT und HELLO.PRG
  - COM1-Bootprotokoll in vmware-serial.log
  - keine virtuelle Diskette, kein USB- oder Audiogerät

Die VMDK-Dateien und die VMX-Datei müssen im selben Ordner bleiben.
VMnet0 verwendet standardmaessig VMwares automatische Bridge-Auswahl. Falls
mehrere physische Netzwerkadapter vorhanden sind, in "Virtual Network Editor"
VMnet0 fest dem gewuenschten Ethernet- oder WLAN-Adapter zuordnen.

LAN-Test in der Kernel-Shell:
  getip             zeigt die per DHCP bezogene LAN-Adresse
  net dhcp          fordert eine neue DHCP-Konfiguration an
  ping 192.168.1.1  testet den Router (Adresse ggf. anpassen)

Shell- und Programmtest:
  dir                zeigt README.TXT und HELLO.PRG
  type README.TXT    liest die Datei ueber den gemeinsamen VFS-Pfad
  run HELLO.PRG      startet das extern gebaute Beispielprogramm

Der aktuelle Minimal-Stack unterstuetzt Ethernet, ARP, IPv4, ICMP und DHCP.
DNS-Anfragen und TCP-Anwendungen wie HTTP/SMB sind noch nicht implementiert.
"""
    (vm_directory / "README-VMWARE.txt").write_text(
        readme, encoding="utf-8", newline="\r\n"
    )
    return vmx


def build_image(stage1_path: Path, stage2_path: Path, kernel_path: Path,
                 output_path: Path, vmdk_path: Path,
                 vmware_directory: Path | None = None,
                 data_files: Mapping[str, bytes] | None = None) -> None:
    stage1 = stage1_path.read_bytes()
    stage2 = stage2_path.read_bytes()
    kernel = kernel_path.read_bytes()
    entry, load_count = validate_elf32(kernel)

    stage2_sectors = sectors_for(len(stage2))
    if not (1 <= stage2_sectors <= STAGE2_MAX_SECTORS):
        raise ValueError(
            f"stage 2 occupies {stage2_sectors} sectors; maximum is {STAGE2_MAX_SECTORS}"
        )
    if STAGE2_RELATIVE_LBA + stage2_sectors > KERNEL_RELATIVE_LBA:
        raise ValueError("stage 2 overlaps the fixed kernel extent")

    total_sectors = IMAGE_SIZE // SECTOR_SIZE
    partition_sectors = DATA_PARTITION_START - PARTITION_START
    if KERNEL_RELATIVE_LBA + sectors_for(len(kernel)) > partition_sectors:
        raise ValueError("kernel does not fit in the raw boot partition")

    mbr = patch_partition_table(stage1, partition_sectors, total_sectors)
    manifest = create_manifest(stage2_sectors, kernel, partition_sectors)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as image:
        image.truncate(IMAGE_SIZE)
        image.seek(0)
        image.write(mbr)
        partition_offset = PARTITION_START * SECTOR_SIZE
        image.seek(partition_offset)
        image.write(manifest)
        image.seek(partition_offset + STAGE2_RELATIVE_LBA * SECTOR_SIZE)
        image.write(stage2)
        image.seek(partition_offset + KERNEL_RELATIVE_LBA * SECTOR_SIZE)
        image.write(kernel)
        write_fat32_volume(
            image,
            DATA_PARTITION_START,
            total_sectors - DATA_PARTITION_START,
            binascii.crc32(kernel) & 0xFFFFFFFF,
            data_files,
        )

    kernel_crc = binascii.crc32(kernel) & 0xFFFFFFFF
    write_vmdk_descriptor(vmdk_path, output_path, total_sectors, kernel_crc)
    vmx_path = vmdk_path.with_suffix(".vmx")
    write_vmx(vmx_path, vmdk_path)
    packaged_vmx = None
    if vmware_directory is not None:
        packaged_vmx = write_vmware_package(
            vmware_directory, output_path, total_sectors, kernel_crc
        )
    messages = [
        f"Native BIOS image: {output_path} ({IMAGE_SIZE // (1024 * 1024)} MiB)",
        f"VMware descriptor: {vmdk_path}",
        f"VMware machine: {vmx_path}",
    ]
    if packaged_vmx is not None:
        messages.append(f"Ready-to-run VMware package: {packaged_vmx}")
    messages.append(
        f"Kernel: {len(kernel)} bytes, {load_count} PT_LOAD segments, "
        f"entry 0x{entry:08X}"
    )
    print("\n".join(messages))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage1", required=True, type=Path)
    parser.add_argument("--stage2", required=True, type=Path)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--vmdk", required=True, type=Path)
    parser.add_argument("--vmware-dir", type=Path)
    parser.add_argument(
        "--data-file",
        action="append",
        default=[],
        metavar="NAME=PATH",
        help="embed PATH in the FAT32 root directory under an 8.3 NAME",
    )
    args = parser.parse_args()

    data_files: dict[str, bytes] = {}
    seen_names: set[bytes] = set()
    for specification in args.data_file:
        if "=" not in specification:
            parser.error(f"--data-file must use NAME=PATH: {specification!r}")
        name, path_text = specification.split("=", 1)
        if not name or not path_text:
            parser.error(f"--data-file must use NAME=PATH: {specification!r}")
        try:
            short_name = fat32_short_name(name)
        except ValueError as error:
            parser.error(str(error))
        if short_name == fat32_short_name("README.TXT") or short_name in seen_names:
            parser.error(f"duplicate FAT32 filename: {name!r}")
        try:
            contents = Path(path_text).read_bytes()
        except OSError as error:
            parser.error(f"cannot read --data-file {path_text!r}: {error}")
        seen_names.add(short_name)
        data_files[name] = contents

    build_image(
        args.stage1,
        args.stage2,
        args.kernel,
        args.output,
        args.vmdk,
        args.vmware_dir,
        data_files,
    )


if __name__ == "__main__":
    main()
