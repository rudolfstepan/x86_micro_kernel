#!/usr/bin/env python3
"""Create one bounded authenticated REIST offline boot-update bundle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import tempfile
import zlib
from pathlib import Path

try:
    from scripts.create_native_boot_image import validate_elf32
    from scripts.verify_boot_signature import verify_signature
except ModuleNotFoundError:
    from create_native_boot_image import validate_elf32
    from verify_boot_signature import verify_signature


BUNDLE_MAGIC = b"REISTUP1"
BUNDLE_VERSION = 1
BUNDLE_HEADER_SIZE = 512
BUNDLE_SIGNATURE_SIZE = 256
BUNDLE_ALGORITHM_RSA_PSS_SHA256 = 1
BUNDLE_KERNEL_CAPACITY = 3008 * 512
BUNDLE_DIGEST_OFFSET = 40
BUNDLE_SIGNATURE_OFFSET = 72
BUNDLE_POLICY_FINGERPRINT_OFFSET = 328
BUNDLE_RESERVED_OFFSET = 360
BUNDLE_CRC_OFFSET = 508


def _policy_fingerprint(policy_path: Path) -> bytes:
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    value = policy.get("public_key_spki_sha256")
    if not isinstance(value, str) or len(value) != 64 or value != value.lower():
        raise ValueError("trust-policy SPKI fingerprint is not canonical SHA-256")
    try:
        fingerprint = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError("trust-policy SPKI fingerprint is not hexadecimal") \
            from error
    if len(fingerprint) != 32:
        raise ValueError("trust-policy SPKI fingerprint is not SHA-256")
    return fingerprint


def create_update_bundle(
        kernel_path: Path, signature_path: Path, output_path: Path,
        policy_path: Path, openssl: Path, root: Path) -> str:
    kernel = kernel_path.read_bytes()
    signature = signature_path.read_bytes()
    if not kernel or len(kernel) > BUNDLE_KERNEL_CAPACITY:
        raise ValueError("kernel exceeds the fixed update-bundle capacity")
    validate_elf32(kernel)
    if len(signature) != BUNDLE_SIGNATURE_SIZE:
        raise ValueError("update-bundle signature is not exactly 256 bytes")
    digest = hashlib.sha256(kernel).digest()
    verified = verify_signature(
        kernel_path, signature_path, policy_path, openssl, root
    )
    if verified != digest.hex():
        raise ValueError("signature verifier returned a different kernel digest")
    fingerprint = _policy_fingerprint(policy_path)

    total_size = BUNDLE_HEADER_SIZE + len(kernel)
    header = bytearray(BUNDLE_HEADER_SIZE)
    struct.pack_into(
        "<8sIIQIIII", header, 0, BUNDLE_MAGIC, BUNDLE_VERSION,
        BUNDLE_HEADER_SIZE, total_size, len(kernel), BUNDLE_SIGNATURE_SIZE,
        BUNDLE_ALGORITHM_RSA_PSS_SHA256, 0,
    )
    header[BUNDLE_DIGEST_OFFSET:BUNDLE_SIGNATURE_OFFSET] = digest
    header[BUNDLE_SIGNATURE_OFFSET:BUNDLE_POLICY_FINGERPRINT_OFFSET] = signature
    header[BUNDLE_POLICY_FINGERPRINT_OFFSET:BUNDLE_RESERVED_OFFSET] = fingerprint
    struct.pack_into("<I", header, BUNDLE_CRC_OFFSET, zlib.crc32(header))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            if stream.write(header) != len(header) or \
                    stream.write(kernel) != len(kernel):
                raise OSError("short update-bundle write")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output_path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return digest.hex()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=Path("openssl"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    try:
        digest = create_update_bundle(
            args.kernel, args.signature, args.output, args.policy,
            args.openssl, args.root,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"BOOT UPDATE BUNDLE CREATE FAIL: {error}")
        return 1
    print(
        f"BOOT UPDATE BUNDLE CREATE PASS: kernel_sha256={digest} "
        f"output={args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
