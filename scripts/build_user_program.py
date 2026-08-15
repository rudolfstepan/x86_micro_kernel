#!/usr/bin/env python3
"""Build external C/assembly sources into the kernel's fixed-address MYPR format."""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROGRAM_BASE = 0x40000000
PROGRAM_REGION_SIZE = 8 * 1024 * 1024
PROGRAM_HEADER = struct.Struct("<4s6I")
PAYLOAD_BASE = PROGRAM_BASE + PROGRAM_HEADER.size
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
ELF_PROGRAM_HEADER = struct.Struct("<IIIIIIII")
ELF_SECTION_HEADER = struct.Struct("<IIIIIIIIII")


def find_zig(explicit: Path | None = None) -> Path:
    candidates = [
        explicit,
        Path(shutil.which("zig")) if shutil.which("zig") else None,
        Path(r"C:\tmp\zig-0.16.0-portable\zig-x86_64-windows-0.16.0\zig.exe"),
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "Zig was not found. Install Zig or pass --zig C:\\path\\to\\zig.exe"
    )


def run(command: list[str], environment: dict[str, str]) -> None:
    subprocess.run(command, check=True, cwd=ROOT, env=environment)


def elf_to_mypr(elf: bytes) -> bytes:
    if len(elf) < ELF_HEADER.size:
        raise ValueError("linked program is not a complete ELF32 file")
    values = ELF_HEADER.unpack_from(elf)
    (ident, elf_type, machine, version, entry, phoff, shoff, _flags,
     ehsize, phentsize, phnum, shentsize, shnum, _shstrndx) = values
    if ident[:7] != b"\x7fELF\x01\x01\x01" or elf_type != 2 or machine != 3:
        raise ValueError("linked program must be a little-endian i386 executable")
    if version != 1 or ehsize != ELF_HEADER.size or phentsize != 32:
        raise ValueError("unsupported ELF32 header")
    if phoff + phnum * phentsize > len(elf):
        raise ValueError("truncated ELF32 program-header table")

    if shnum and shoff:
        if shentsize != ELF_SECTION_HEADER.size or \
                shoff + shnum * shentsize > len(elf):
            raise ValueError("truncated ELF32 section-header table")
        for index in range(shnum):
            section = ELF_SECTION_HEADER.unpack_from(
                elf, shoff + index * shentsize
            )
            section_type, section_size = section[1], section[5]
            if section_type in (4, 9) and section_size != 0:
                raise ValueError("runtime ELF relocations are not supported")

    segments: list[tuple[int, int, int, int, int]] = []
    image_end = PAYLOAD_BASE
    file_end = PAYLOAD_BASE
    entry_is_executable = False
    for index in range(phnum):
        (segment_type, file_offset, virtual_address, physical_address,
         file_size, memory_size, flags, _alignment) = \
            ELF_PROGRAM_HEADER.unpack_from(elf, phoff + index * phentsize)
        if segment_type != 1 or memory_size == 0:
            continue
        if file_size > memory_size or file_offset + file_size > len(elf):
            raise ValueError(f"invalid PT_LOAD segment {index}")
        if virtual_address != physical_address:
            raise ValueError("user program must use identical virtual/physical addresses")
        if virtual_address < PAYLOAD_BASE:
            raise ValueError("a PT_LOAD segment overlaps the MYPR header")
        segment_end = virtual_address + memory_size
        if segment_end < virtual_address or segment_end - PROGRAM_BASE > PROGRAM_REGION_SIZE:
            raise ValueError("user program exceeds the 8 MiB loader region")
        # MYPR v1 has no segment table at runtime.  Its validator can only
        # accept an entry point backed by bytes stored in the PRG file, never
        # an address in a zero-filled executable segment tail.
        if flags & 1 and virtual_address <= entry < virtual_address + file_size:
            entry_is_executable = True
        segments.append((virtual_address, file_offset, file_size, memory_size, flags))
        image_end = max(image_end, segment_end)
        file_end = max(file_end, virtual_address + file_size)

    if not segments or not entry_is_executable:
        raise ValueError("program entry point is not in an executable PT_LOAD segment")

    payload = bytearray(file_end - PAYLOAD_BASE)
    occupied = bytearray(image_end - PAYLOAD_BASE)
    for virtual_address, file_offset, file_size, memory_size, _flags in segments:
        destination = virtual_address - PAYLOAD_BASE
        if any(occupied[destination:destination + memory_size]):
            raise ValueError("overlapping PT_LOAD segments are not supported")
        if file_size:
            payload[destination:destination + file_size] = \
                elf[file_offset:file_offset + file_size]
        occupied[destination:destination + memory_size] = b"\x01" * memory_size

    while (PROGRAM_HEADER.size + len(payload)) % 4:
        payload.append(0)
    relocation_offset = PROGRAM_HEADER.size + len(payload)
    memory_payload_size = max(image_end - PAYLOAD_BASE, len(payload))
    header = PROGRAM_HEADER.pack(
        b"MYPR",
        0xDEADBEEF,
        entry - PROGRAM_BASE,
        memory_payload_size,
        PROGRAM_BASE,
        relocation_offset,
        0,
    )
    return header + payload


def build(sources: list[Path], output: Path, zig: Path,
          elf_output: Path | None = None) -> None:
    sdk = ROOT / "userspace" / "sdk"
    linker_script = ROOT / "config" / "user_program.ld"
    all_sources = [sdk / "crt0.c", sdk / "x86os.c",
                   sdk / "reist_dhcp_state.c", *sources]
    if any(source.name == "reist_probe.c" for source in sources):
        all_sources.insert(3, sdk / "reist_ipv4_parser.c")
        all_sources.insert(4, sdk / "reist_udp_parser.c")
        all_sources.insert(5, sdk / "reist_dhcp_parser.c")
    for source in all_sources:
        if not source.is_file():
            raise FileNotFoundError(source)
        if source.suffix.lower() not in (".c", ".s"):
            raise ValueError(f"unsupported source type: {source}")

    with tempfile.TemporaryDirectory(prefix="x86-user-build-") as temporary:
        temporary_path = Path(temporary)
        environment = os.environ.copy()
        environment["ZIG_GLOBAL_CACHE_DIR"] = str(temporary_path / "zig-global")
        environment["ZIG_LOCAL_CACHE_DIR"] = str(temporary_path / "zig-local")
        objects: list[Path] = []
        common_flags = [
            str(zig), "cc", "-target", "x86-freestanding", "-march=i386",
            "-O2", "-DNDEBUG", "-ffreestanding", "-fno-builtin",
            "-fno-pic", "-fno-pie", "-fno-stack-protector",
            "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
            "-mno-sse", "-mno-sse2", "-mno-mmx", "-Wall", "-Wextra",
            "-I", str(sdk / "include"),
        ]
        for index, source in enumerate(all_sources):
            object_path = temporary_path / f"source-{index}.o"
            language_flags = ["-std=c11"] if source.suffix.lower() == ".c" else []
            run([*common_flags, *language_flags, "-c", str(source),
                 "-o", str(object_path)], environment)
            objects.append(object_path)

        elf_path = temporary_path / "program.elf"
        run([
            str(zig), "ld.lld", "-m", "elf_i386", "-T", str(linker_script),
            "--gc-sections", "--strip-all", "-o", str(elf_path),
            *(str(value) for value in objects),
        ], environment)
        elf = elf_path.read_bytes()
        program = elf_to_mypr(elf)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(program)
        if elf_output:
            elf_output.parent.mkdir(parents=True, exist_ok=True)
            elf_output.write_bytes(elf)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile external sources into a loadable REIST OS PRG"
    )
    parser.add_argument("sources", nargs="+", type=Path)
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("--elf-output", type=Path)
    parser.add_argument("--zig", type=Path)
    args = parser.parse_args()
    zig = find_zig(args.zig)
    build([source.resolve() for source in args.sources], args.output.resolve(),
          zig, args.elf_output.resolve() if args.elf_output else None)
    print(f"User program: {args.output.resolve()}")


if __name__ == "__main__":
    main()
