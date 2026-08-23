#!/usr/bin/env python3
"""Independently parse and authenticate a bounded REIST update bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    from scripts.create_native_boot_image import validate_elf32
    from scripts.verify_boot_signature import verify_signature
except ModuleNotFoundError:
    from create_native_boot_image import validate_elf32
    from verify_boot_signature import verify_signature


VERIFY_MAGIC = b"REISTUP1"
VERIFY_VERSION = 1
VERIFY_HEADER_SIZE = 512
VERIFY_SIGNATURE_SIZE = 256
VERIFY_ALGORITHM_RSA_PSS_SHA256 = 1
VERIFY_KERNEL_CAPACITY = 3008 * 512
VERIFY_DIGEST_OFFSET = 40
VERIFY_SIGNATURE_OFFSET = 72
VERIFY_POLICY_FINGERPRINT_OFFSET = 328
VERIFY_RESERVED_OFFSET = 360
VERIFY_CRC_OFFSET = 508


@dataclass(frozen=True)
class VerifiedUpdateBundle:
    kernel: bytes
    signature: bytes
    kernel_sha256: str
    policy_spki_sha256: str


def _local_policy_fingerprint(policy_path: Path) -> bytes:
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    value = policy.get("public_key_spki_sha256")
    if not isinstance(value, str) or len(value) != 64 or value != value.lower():
        raise ValueError("local trust-policy SPKI fingerprint is not canonical")
    try:
        fingerprint = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError("local trust-policy SPKI fingerprint is malformed") \
            from error
    if len(fingerprint) != 32:
        raise ValueError("local trust-policy SPKI fingerprint is not SHA-256")
    return fingerprint


def verify_update_bundle(
        bundle_path: Path, policy_path: Path, openssl: Path,
        root: Path) -> VerifiedUpdateBundle:
    size = bundle_path.stat().st_size
    if size < VERIFY_HEADER_SIZE or \
            size > VERIFY_HEADER_SIZE + VERIFY_KERNEL_CAPACITY:
        raise ValueError("update-bundle size is outside the fixed capacity")
    bundle = bundle_path.read_bytes()
    if len(bundle) != size:
        raise OSError("short update-bundle read")
    header = bytearray(bundle[:VERIFY_HEADER_SIZE])
    magic, version, header_size, total_size, kernel_size, signature_size, \
        algorithm, flags = struct.unpack_from("<8sIIQIIII", header, 0)
    if magic != VERIFY_MAGIC or version != VERIFY_VERSION or \
            header_size != VERIFY_HEADER_SIZE:
        raise ValueError("update-bundle format is unsupported")
    if total_size != size or kernel_size == 0 or \
            kernel_size > VERIFY_KERNEL_CAPACITY or \
            total_size != VERIFY_HEADER_SIZE + kernel_size:
        raise ValueError("update-bundle geometry is inconsistent")
    if signature_size != VERIFY_SIGNATURE_SIZE or \
            algorithm != VERIFY_ALGORITHM_RSA_PSS_SHA256 or flags != 0:
        raise ValueError("update-bundle algorithm or flags are unsupported")
    if any(header[VERIFY_RESERVED_OFFSET:VERIFY_CRC_OFFSET]):
        raise ValueError("update-bundle reserved bytes are not zero")
    stored_crc, = struct.unpack_from("<I", header, VERIFY_CRC_OFFSET)
    struct.pack_into("<I", header, VERIFY_CRC_OFFSET, 0)
    if stored_crc != zlib.crc32(header):
        raise ValueError("update-bundle header CRC32 mismatch")

    expected_digest = bytes(
        header[VERIFY_DIGEST_OFFSET:VERIFY_SIGNATURE_OFFSET]
    )
    signature = bytes(
        header[VERIFY_SIGNATURE_OFFSET:VERIFY_POLICY_FINGERPRINT_OFFSET]
    )
    bundled_fingerprint = bytes(
        header[VERIFY_POLICY_FINGERPRINT_OFFSET:VERIFY_RESERVED_OFFSET]
    )
    local_fingerprint = _local_policy_fingerprint(policy_path)
    if bundled_fingerprint != local_fingerprint:
        raise ValueError("update-bundle policy fingerprint mismatch")
    kernel = bundle[VERIFY_HEADER_SIZE:]
    actual_digest = hashlib.sha256(kernel).digest()
    if expected_digest != actual_digest:
        raise ValueError("update-bundle kernel SHA-256 mismatch")
    validate_elf32(kernel)

    with tempfile.TemporaryDirectory(prefix="reist-update-verify-") as directory:
        artifact = Path(directory) / "kernel.bin"
        detached = Path(directory) / "kernel.bin.sig"
        artifact.write_bytes(kernel)
        detached.write_bytes(signature)
        verified = verify_signature(
            artifact, detached, policy_path, openssl, root
        )
    if verified != actual_digest.hex():
        raise ValueError("bundle signature verifier returned a different digest")
    return VerifiedUpdateBundle(
        kernel, signature, actual_digest.hex(), local_fingerprint.hex()
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=Path("openssl"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    try:
        verified = verify_update_bundle(
            args.bundle, args.policy, args.openssl, args.root
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"BOOT UPDATE BUNDLE VERIFY FAIL: {error}")
        return 1
    print(
        "BOOT UPDATE BUNDLE VERIFY PASS: "
        f"kernel_sha256={verified.kernel_sha256}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
