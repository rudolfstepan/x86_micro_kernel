#!/usr/bin/env python3
"""Independently verify a policy-pinned RSA-PSS boot-artifact signature."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _run(command: list[str]) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )


def _resolve(root: Path, value: object) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError("trust-policy public_key is missing")
    path = Path(value)
    return path if path.is_absolute() else root / path


def verify_signature(
    artifact: Path,
    signature: Path,
    policy_path: Path,
    openssl: Path,
    root: Path,
) -> str:
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    exact = {
        "schema_version": 1,
        "signature_algorithm": "RSA-2048-PSS-SHA256",
        "rsa_modulus_bits": 2048,
        "mgf1_digest": "SHA256",
        "salt_length_bytes": 32,
        "signature_size_bytes": 256,
    }
    for name, expected in exact.items():
        if policy.get(name) != expected:
            raise ValueError(f"unsupported trust-policy {name}")
    if policy.get("stage2_signature_verification") is not True:
        raise ValueError("trust-policy must require stage-2 signature verification")
    if not artifact.is_file() or artifact.stat().st_size == 0:
        raise ValueError("boot artifact is missing or empty")
    if not signature.is_file() or signature.stat().st_size != 256:
        raise ValueError("RSA-PSS signature is missing or not 256 bytes")

    public_key = _resolve(root, policy.get("public_key"))
    der = _run([
        str(openssl), "pkey", "-pubin", "-in", str(public_key),
        "-outform", "DER",
    ])
    if der.returncode != 0:
        raise ValueError("pinned public key is not readable by OpenSSL")
    fingerprint = hashlib.sha256(der.stdout).hexdigest()
    expected_fingerprint = policy.get("public_key_spki_sha256")
    if fingerprint != expected_fingerprint:
        raise ValueError("pinned public-key fingerprint mismatch")
    details = _run([
        str(openssl), "pkey", "-pubin", "-in", str(public_key),
        "-text", "-noout",
    ])
    match = re.search(rb"Public-Key: \((\d+) bit", details.stdout)
    if details.returncode != 0 or match is None or int(match.group(1)) != 2048:
        raise ValueError("pinned public key is not RSA-2048")

    verified = _run([
        str(openssl), "dgst", "-sha256", "-verify", str(public_key),
        "-signature", str(signature),
        "-sigopt", "rsa_padding_mode:pss",
        "-sigopt", "rsa_pss_saltlen:32",
        "-sigopt", "rsa_mgf1_md:sha256",
        str(artifact),
    ])
    if verified.returncode != 0 or verified.stdout.strip() != b"Verified OK":
        raise ValueError("RSA-PSS signature verification failed")
    return _file_sha256(artifact)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=Path("openssl"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    try:
        digest = verify_signature(
            args.artifact, args.signature, args.policy,
            args.openssl, args.root,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"BOOT SIGNATURE VERIFY FAIL: {error}")
        return 1
    print(f"BOOT SIGNATURE VERIFY PASS: artifact_sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
