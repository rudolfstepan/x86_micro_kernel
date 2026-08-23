#!/usr/bin/env python3
"""Run a bounded deterministic fault campaign over REIST update bundles."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    from scripts.create_boot_update_bundle import (
        BUNDLE_CRC_OFFSET,
        BUNDLE_HEADER_SIZE,
        BUNDLE_KERNEL_CAPACITY,
        create_update_bundle,
    )
    from scripts.update_native_boot_slot import update_inactive_slot_from_bundle
    from scripts.validate_boot_manifest import validate_image
    from scripts.verify_boot_update_bundle import verify_update_bundle
except ModuleNotFoundError:
    from create_boot_update_bundle import (
        BUNDLE_CRC_OFFSET,
        BUNDLE_HEADER_SIZE,
        BUNDLE_KERNEL_CAPACITY,
        create_update_bundle,
    )
    from update_native_boot_slot import update_inactive_slot_from_bundle
    from validate_boot_manifest import validate_image
    from verify_boot_update_bundle import verify_update_bundle


MIN_CASES = 16
MAX_CASES = 128
STRUCTURED_CASES = 16
MAX_SOURCE_IMAGE_SIZE = 128 * 1024 * 1024
MAX_POLICY_SIZE = 64 * 1024


@dataclass(frozen=True)
class FaultCampaignResult:
    cases: int
    seed: int
    structured_cases: int
    random_cases: int
    source_sha256: str


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _repair_crc(bundle: bytearray) -> None:
    struct.pack_into("<I", bundle, BUNDLE_CRC_OFFSET, 0)
    struct.pack_into(
        "<I", bundle, BUNDLE_CRC_OFFSET,
        zlib.crc32(bundle[:BUNDLE_HEADER_SIZE]),
    )


def _structured_mutation(valid: bytes, index: int) -> tuple[str, bytes]:
    changed = bytearray(valid)
    payload_size = len(valid) - BUNDLE_HEADER_SIZE
    if index == 0:
        name = "magic"
        changed[0] ^= 1
    elif index == 1:
        name = "version"
        struct.pack_into("<I", changed, 8, 2)
    elif index == 2:
        name = "header-size"
        struct.pack_into("<I", changed, 12, BUNDLE_HEADER_SIZE - 1)
    elif index == 3:
        name = "total-size"
        struct.pack_into("<Q", changed, 16, len(valid) + 1)
    elif index == 4:
        name = "kernel-size"
        struct.pack_into("<I", changed, 24, payload_size + 1)
    elif index == 5:
        name = "signature-size"
        struct.pack_into("<I", changed, 28, 255)
    elif index == 6:
        name = "algorithm"
        struct.pack_into("<I", changed, 32, 2)
    elif index == 7:
        name = "flags"
        struct.pack_into("<I", changed, 36, 1)
    elif index == 8:
        name = "digest"
        changed[40] ^= 1
    elif index == 9:
        name = "signature"
        changed[72] ^= 1
    elif index == 10:
        name = "policy-fingerprint"
        changed[328] ^= 1
    elif index == 11:
        name = "reserved"
        changed[360] = 1
    elif index == 12:
        name = "header-crc"
        changed[BUNDLE_CRC_OFFSET] ^= 1
        return name, bytes(changed)
    elif index == 13:
        name = "payload"
        changed[BUNDLE_HEADER_SIZE + payload_size // 2] ^= 1
    elif index == 14:
        return "truncated", bytes(changed[:-1])
    elif index == 15:
        return "trailing-data", bytes(changed + b"x")
    else:
        raise ValueError("structured fault index is out of range")
    _repair_crc(changed)
    return name, bytes(changed)


def _random_points(seed: int, count: int, bit_count: int) -> list[int]:
    state = seed
    points = []
    used = set()
    for _ in range(count):
        for _retry in range(MAX_CASES * 2):
            state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
            point = state % bit_count
            if point not in used:
                used.add(point)
                points.append(point)
                break
        else:
            raise AssertionError("bounded mutation-point selection exhausted")
    return points


def _validate_input_sizes(
        image: Path, kernel: Path, signature: Path, policy: Path) -> None:
    for path in (image, kernel, signature, policy):
        if not path.is_file():
            raise ValueError(f"fault campaign input is not a file: {path}")
    if image.stat().st_size <= 0 or \
            image.stat().st_size > MAX_SOURCE_IMAGE_SIZE:
        raise ValueError("fault campaign image exceeds 128 MiB")
    if kernel.stat().st_size <= 0 or \
            kernel.stat().st_size > BUNDLE_KERNEL_CAPACITY:
        raise ValueError("fault campaign kernel exceeds the slot capacity")
    if signature.stat().st_size != 256:
        raise ValueError("fault campaign signature is not exactly 256 bytes")
    if policy.stat().st_size <= 0 or policy.stat().st_size > MAX_POLICY_SIZE:
        raise ValueError("fault campaign policy exceeds 64 KiB")


def run_fault_campaign(
        image: Path, kernel: Path, signature: Path, policy: Path,
        openssl: Path, root: Path, cases: int, seed: int) -> FaultCampaignResult:
    if cases < MIN_CASES or cases > MAX_CASES:
        raise ValueError("fault campaign cases must be in [16, 128]")
    if seed < 0 or seed > 0xFFFFFFFF:
        raise ValueError("fault campaign seed must be an unsigned 32-bit value")
    _validate_input_sizes(image, kernel, signature, policy)
    validate_image(image, "hdd")
    originals = {
        image: _file_sha256(image),
        kernel: _file_sha256(kernel),
        signature: _file_sha256(signature),
    }

    with tempfile.TemporaryDirectory(prefix="reist-update-faults-") as directory:
        temporary = Path(directory)
        valid_bundle = temporary / "valid.rup"
        create_update_bundle(
            kernel, signature, valid_bundle, policy, openssl, root
        )
        verify_update_bundle(valid_bundle, policy, openssl, root)
        valid = valid_bundle.read_bytes()
        random_count = cases - STRUCTURED_CASES
        points = _random_points(seed, random_count, len(valid) * 8)

        for case_index in range(cases):
            if case_index < STRUCTURED_CASES:
                name, mutation = _structured_mutation(valid, case_index)
            else:
                point = points[case_index - STRUCTURED_CASES]
                changed = bytearray(valid)
                changed[point // 8] ^= 1 << (point % 8)
                name = f"bit-{point}"
                mutation = bytes(changed)
            malformed = temporary / f"case-{case_index:03d}-{name}.rup"
            output = temporary / f"case-{case_index:03d}.img"
            malformed.write_bytes(mutation)
            try:
                update_inactive_slot_from_bundle(
                    image, malformed, output, policy, openssl, root
                )
            except (OSError, ValueError):
                pass
            else:
                raise AssertionError(
                    f"fault case {case_index} ({name}) was accepted"
                )
            if output.exists():
                raise AssertionError(
                    f"fault case {case_index} ({name}) created an output image"
                )

    for path, expected in originals.items():
        if _file_sha256(path) != expected:
            raise AssertionError(f"fault campaign changed signed input {path}")
    return FaultCampaignResult(
        cases, seed, STRUCTURED_CASES, cases - STRUCTURED_CASES,
        originals[image],
    )


def _bounded_cases(value: str) -> int:
    parsed = int(value, 10)
    if parsed < MIN_CASES or parsed > MAX_CASES:
        raise argparse.ArgumentTypeError("cases must be in [16, 128]")
    return parsed


def _uint32(value: str) -> int:
    parsed = int(value, 0)
    if parsed < 0 or parsed > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("seed must be an unsigned 32-bit value")
    return parsed


def _openssl_path(value: Path | None) -> Path:
    if value is not None:
        return value
    found = shutil.which("openssl")
    fallback = Path(r"C:\msys64\mingw64\bin\openssl.exe")
    if found:
        return Path(found)
    if fallback.is_file():
        return fallback
    raise ValueError("OpenSSL is required for the boot-update fault campaign")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--openssl", type=Path)
    parser.add_argument("--cases", type=_bounded_cases, default=64)
    parser.add_argument("--seed", type=_uint32, required=True)
    args = parser.parse_args()
    try:
        result = run_fault_campaign(
            args.image, args.kernel, args.signature, args.policy,
            _openssl_path(args.openssl), args.root, args.cases, args.seed,
        )
    except (AssertionError, OSError, ValueError) as error:
        print(f"BOOT_UPDATE_FAULT_CAMPAIGN FAIL: {error}")
        return 1
    print(
        "BOOT_UPDATE_FAULT_CAMPAIGN PASS "
        f"cases={result.cases} structured={result.structured_cases} "
        f"random={result.random_cases} seed={result.seed} "
        f"source_sha256={result.source_sha256}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
