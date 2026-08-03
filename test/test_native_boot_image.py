import struct
import tempfile
import unittest
from pathlib import Path

from scripts.create_native_boot_image import (
    DATA_PARTITION_START,
    DATA_PARTITION_TYPE,
    IMAGE_SIZE,
    MANIFEST_MAGIC,
    PARTITION_START,
    PARTITION_TYPE,
    create_manifest,
    patch_partition_table,
    validate_elf32,
    write_fat32_volume,
    write_vmware_package,
    write_vmdk_descriptor,
    write_vmx,
)


def minimal_kernel(size=4096):
    kernel = bytearray(size)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into(
        "<16sHHIIIIIHHHHHH",
        kernel,
        0,
        ident,
        2,
        3,
        1,
        0x00100000,
        52,
        0,
        0,
        52,
        32,
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        "<IIIIIIII",
        kernel,
        52,
        1,
        84,
        0x00100000,
        0x00100000,
        32,
        64,
        5,
        4096,
    )
    return bytes(kernel)


def read_fat32_chain(image, partition_lba, boot, fat, start_cluster):
    if start_cluster == 0:
        return [], b""
    bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
    sectors_per_cluster = boot[13]
    reserved_sectors = struct.unpack_from("<H", boot, 14)[0]
    fat_count = boot[16]
    fat_sectors = struct.unpack_from("<I", boot, 36)[0]
    total_sectors = struct.unpack_from("<I", boot, 32)[0]
    first_data_sector = reserved_sectors + fat_count * fat_sectors
    cluster_count = (
        total_sectors - first_data_sector
    ) // sectors_per_cluster
    cluster_size = bytes_per_sector * sectors_per_cluster

    clusters = []
    contents = bytearray()
    seen = set()
    cluster = start_cluster
    while True:
        if cluster in seen:
            raise AssertionError("cyclic FAT32 cluster chain")
        if not 2 <= cluster < cluster_count + 2:
            raise AssertionError("FAT32 cluster lies outside the data area")
        seen.add(cluster)
        clusters.append(cluster)
        relative_sector = (
            first_data_sector + (cluster - 2) * sectors_per_cluster
        )
        image.seek((partition_lba + relative_sector) * bytes_per_sector)
        contents.extend(image.read(cluster_size))
        successor = struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
        if successor >= 0x0FFFFFF8:
            break
        if successor == 0 or successor == 0x0FFFFFF7:
            raise AssertionError("invalid FAT32 cluster-chain successor")
        cluster = successor
    return clusters, bytes(contents)


def read_fat32_root_files(image, partition_lba):
    image.seek(partition_lba * 512)
    boot = image.read(512)
    reserved_sectors = struct.unpack_from("<H", boot, 14)[0]
    fat_sectors = struct.unpack_from("<I", boot, 36)[0]
    fat_size = fat_sectors * 512
    image.seek((partition_lba + reserved_sectors) * 512)
    first_fat = image.read(fat_size)
    image.seek((partition_lba + reserved_sectors + fat_sectors) * 512)
    second_fat = image.read(fat_size)
    if first_fat != second_fat:
        raise AssertionError("FAT32 copies differ")

    root_cluster = struct.unpack_from("<I", boot, 44)[0]
    root_chain, root_data = read_fat32_chain(
        image, partition_lba, boot, first_fat, root_cluster
    )
    files = {}
    for offset in range(0, len(root_data), 32):
        entry = root_data[offset:offset + 32]
        if entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
            continue
        start_cluster = (
            struct.unpack_from("<H", entry, 20)[0] << 16
            | struct.unpack_from("<H", entry, 26)[0]
        )
        size = struct.unpack_from("<I", entry, 28)[0]
        chain, data = read_fat32_chain(
            image, partition_lba, boot, first_fat, start_cluster
        )
        files[entry[0:11]] = (data[:size], chain, size)
    return boot, first_fat, root_chain, files


class NativeBootImageTests(unittest.TestCase):
    def test_accepts_supported_identity_mapped_elf32(self):
        self.assertEqual(validate_elf32(minimal_kernel()), (0x00100000, 1))

    def test_rejects_kernel_smaller_than_stage2_header_read(self):
        with self.assertRaisesRegex(ValueError, "4 KiB"):
            validate_elf32(minimal_kernel(512))

    def test_manifest_has_magic_and_zero_additive_checksum(self):
        manifest = create_manifest(5, minimal_kernel(), 8192)
        self.assertEqual(manifest[:8], MANIFEST_MAGIC)
        self.assertEqual(sum(struct.unpack("<128I", manifest)) & 0xFFFFFFFF, 0)

    def test_partition_table_contains_boot_and_fat32_partitions(self):
        stage1 = bytearray(512)
        stage1[510:512] = b"\x55\xAA"
        total_sectors = IMAGE_SIZE // 512
        boot_sectors = DATA_PARTITION_START - PARTITION_START
        mbr = patch_partition_table(bytes(stage1), boot_sectors, total_sectors)
        boot = mbr[446:462]
        data = mbr[462:478]
        self.assertEqual(boot[0], 0x80)
        self.assertEqual(boot[1:4], b"\x00\x21\x02")
        self.assertEqual(boot[4], PARTITION_TYPE)
        self.assertEqual(struct.unpack_from("<I", boot, 8)[0], PARTITION_START)
        self.assertEqual(struct.unpack_from("<I", boot, 12)[0], boot_sectors)
        self.assertEqual(data[0], 0x00)
        self.assertEqual(data[4], DATA_PARTITION_TYPE)
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], DATA_PARTITION_START)
        self.assertEqual(
            struct.unpack_from("<I", data, 12)[0], total_sectors - DATA_PARTITION_START
        )
        self.assertEqual(mbr[478:510], bytes(32))

    def test_embedded_data_partition_is_valid_minimal_fat32(self):
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "disk.img"
            with image_path.open("w+b") as image:
                image.truncate(IMAGE_SIZE)
                total = IMAGE_SIZE // 512 - DATA_PARTITION_START
                write_fat32_volume(image, DATA_PARTITION_START, total, 0x12345678)
                image.seek(DATA_PARTITION_START * 512)
                boot = image.read(512)
                fat_sectors = struct.unpack_from("<I", boot, 36)[0]
                image.seek((DATA_PARTITION_START + 32) * 512)
                fat_prefix = image.read(16)
            self.assertEqual(boot[3:11], b"X86MICRO")
            self.assertEqual(struct.unpack_from("<H", boot, 11)[0], 512)
            self.assertEqual(boot[13], 1)
            self.assertEqual(struct.unpack_from("<H", boot, 14)[0], 32)
            self.assertEqual(boot[16], 2)
            self.assertEqual(struct.unpack_from("<I", boot, 32)[0], total)
            self.assertEqual(fat_sectors, 945)
            self.assertEqual(boot[82:90], b"FAT32   ")
            self.assertEqual(boot[510:512], b"\x55\xAA")
            self.assertEqual(
                struct.unpack("<IIII", fat_prefix),
                (0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF),
            )

    def test_embeds_and_reconstructs_multicluster_83_file(self):
        payload = bytes((index * 37 + 11) & 0xFF for index in range(1307))
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "disk.img"
            with image_path.open("w+b") as image:
                image.truncate(IMAGE_SIZE)
                total = IMAGE_SIZE // 512 - DATA_PARTITION_START
                write_fat32_volume(
                    image,
                    DATA_PARTITION_START,
                    total,
                    0x12345678,
                    {"hello.prg": payload},
                )
                boot, fat, root_chain, files = read_fat32_root_files(
                    image, DATA_PARTITION_START
                )
                image.seek((DATA_PARTITION_START + 1) * 512)
                fsinfo = image.read(512)

        self.assertEqual(root_chain, [2])
        self.assertIn(b"README  TXT", files)
        self.assertIn(b"HELLO   PRG", files)
        embedded, file_chain, size = files[b"HELLO   PRG"]
        self.assertEqual(size, len(payload))
        self.assertEqual(embedded, payload)
        self.assertEqual(len(file_chain), 3)
        self.assertEqual(
            [struct.unpack_from("<I", fat, cluster * 4)[0] & 0x0FFFFFFF
             for cluster in file_chain],
            [file_chain[1], file_chain[2], 0x0FFFFFFF],
        )

        fat_sectors = struct.unpack_from("<I", boot, 36)[0]
        first_data_sector = 32 + 2 * fat_sectors
        cluster_count = (struct.unpack_from("<I", boot, 32)[0]
                         - first_data_sector)
        self.assertEqual(
            struct.unpack_from("<I", fsinfo, 488)[0],
            cluster_count - 5,
        )
        self.assertEqual(struct.unpack_from("<I", fsinfo, 492)[0], 7)

    def test_root_directory_chain_grows_for_many_data_files(self):
        extra_files = {f"F{index:02}.TXT": b"" for index in range(16)}
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "disk.img"
            with image_path.open("w+b") as image:
                image.truncate(IMAGE_SIZE)
                total = IMAGE_SIZE // 512 - DATA_PARTITION_START
                write_fat32_volume(
                    image,
                    DATA_PARTITION_START,
                    total,
                    0x12345678,
                    extra_files,
                )
                _, _, root_chain, files = read_fat32_root_files(
                    image, DATA_PARTITION_START
                )

        self.assertEqual(root_chain, [2, 3])
        self.assertEqual(len(files), 17)
        self.assertEqual(files[b"README  TXT"][0][:3], b"x86")
        for index in range(16):
            name = f"F{index:02}".encode("ascii").ljust(8, b" ") + b"TXT"
            self.assertEqual(files[name], (b"", [], 0))

    def test_rejects_invalid_or_duplicate_data_file_names(self):
        with tempfile.TemporaryDirectory() as directory:
            image_path = Path(directory) / "disk.img"
            with image_path.open("w+b") as image:
                image.truncate(IMAGE_SIZE)
                total = IMAGE_SIZE // 512 - DATA_PARTITION_START
                with self.assertRaisesRegex(ValueError, "8.3"):
                    write_fat32_volume(
                        image,
                        DATA_PARTITION_START,
                        total,
                        0x12345678,
                        {"TOO-LONG9.PRG": b"program"},
                    )
                with self.assertRaisesRegex(ValueError, "duplicate"):
                    write_fat32_volume(
                        image,
                        DATA_PARTITION_START,
                        total,
                        0x12345678,
                        {"readme.txt": b"replacement"},
                    )

    def test_generated_vmware_machine_uses_bios_ide_and_serial_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            vmx = Path(directory) / "kernel.vmx"
            write_vmx(vmx, Path("kernel.vmdk"))
            text = vmx.read_text(encoding="ascii")
        self.assertIn('firmware = "bios"', text)
        self.assertIn('bios.bootOrder = "hdd"', text)
        self.assertIn('memsize = "512"', text)
        self.assertIn('ide0:0.fileName = "kernel.vmdk"', text)
        self.assertIn('serial0.fileName = "vmware-serial.log"', text)
        self.assertIn('serial0.tryNoRxLoss = "TRUE"', text)
        self.assertIn('ethernet0.virtualDev = "e1000"', text)
        self.assertIn('ethernet0.connectionType = "custom"', text)
        self.assertIn('ethernet0.vnet = "VMnet0"', text)

    def test_vmdk_geometry_matches_vmware_legacy_ide_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            vmdk = Path(directory) / "kernel.vmdk"
            write_vmdk_descriptor(vmdk, Path("kernel.img"), 131072, 1)
            text = vmdk.read_text(encoding="ascii")
        self.assertIn('RW 131072 FLAT "kernel.img" 0', text)
        self.assertIn('ddb.geometry.cylinders = "130"', text)
        self.assertIn('ddb.geometry.heads = "16"', text)
        self.assertIn('ddb.geometry.sectors = "63"', text)

    def test_vmware_package_is_self_contained_and_has_launcher(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "source.img"
            raw.write_bytes(bytes(1024))
            package = root / "vm"
            vmx = write_vmware_package(package, raw, 2, 0x12345678)
            self.assertEqual(vmx, package / "x86-microkernel.vmx")
            self.assertEqual(
                (package / "x86-microkernel-flat.vmdk").read_bytes(),
                raw.read_bytes(),
            )
            descriptor = (package / "x86-microkernel.vmdk").read_text(
                encoding="ascii"
            )
            self.assertIn('FLAT "x86-microkernel-flat.vmdk" 0', descriptor)
            self.assertTrue((package / "START-VMWARE.cmd").is_file())
            self.assertTrue((package / "README-VMWARE.txt").is_file())


if __name__ == "__main__":
    unittest.main()
