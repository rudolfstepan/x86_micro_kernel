#!/usr/bin/env python3
"""Create a policy-bound RSA-PSS/SHA-256 signature for a boot artifact."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path


ALGORITHM = "RSA-2048-PSS-SHA256"
EXPECTED_POLICY = {
    "schema_version": 1,
    "signature_algorithm": ALGORITHM,
    "rsa_modulus_bits": 2048,
    "mgf1_digest": "SHA256",
    "salt_length_bytes": 32,
    "signature_size_bytes": 256,
}


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


def _load_policy(path: Path) -> dict[str, object]:
    policy = json.loads(path.read_text(encoding="utf-8"))
    for name, expected in EXPECTED_POLICY.items():
        if policy.get(name) != expected:
            raise ValueError(f"unsupported trust-policy {name}")
    for name in (
        "profile", "public_key", "public_key_spki_sha256",
        "development_key_spki_sha256",
    ):
        if not isinstance(policy.get(name), str) or not policy[name]:
            raise ValueError(f"trust-policy {name} is missing")
    if policy.get("release_requires_external_key") is not True:
        raise ValueError("trust-policy must require an external release key")
    if policy.get("stage2_signature_verification") is not False:
        raise ValueError("host policy must not claim stage-2 signature verification")
    return policy


def _private_key_identity(openssl: Path, private_key: Path) -> tuple[str, int]:
    public = _run([
        str(openssl), "pkey", "-in", str(private_key),
        "-pubout", "-outform", "DER",
    ])
    if public.returncode != 0:
        raise ValueError("private key is not readable by OpenSSL")
    details = _run([
        str(openssl), "pkey", "-in", str(private_key), "-text", "-noout",
    ])
    match = re.search(rb"Private-Key: \((\d+) bit", details.stdout)
    if details.returncode != 0 or match is None:
        raise ValueError("private-key modulus size is unavailable")
    return hashlib.sha256(public.stdout).hexdigest(), int(match.group(1))


def sign_artifact(
    artifact: Path,
    signature: Path,
    private_key: Path,
    policy_path: Path,
    openssl: Path,
    profile: str,
) -> str:
    policy = _load_policy(policy_path)
    if profile == "research":
        if policy["profile"] != "REIST-research-development":
            raise ValueError("research signing requires the research policy")
    elif profile == "release":
        if policy["profile"] != "REIST-release":
            raise ValueError("release signing requires an external release policy")
    else:
        raise ValueError("unsupported signing profile")
    if not artifact.is_file() or artifact.stat().st_size == 0:
        raise ValueError("boot artifact is missing or empty")

    fingerprint, bits = _private_key_identity(openssl, private_key)
    if bits != policy["rsa_modulus_bits"]:
        raise ValueError("private key is not RSA-2048")
    if fingerprint != policy["public_key_spki_sha256"]:
        raise ValueError("private key does not match the pinned public key")
    if (profile == "release" and
            fingerprint == policy["development_key_spki_sha256"]):
        raise ValueError("release mode rejects the development private key")

    signature.parent.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.NamedTemporaryFile(
        prefix=signature.name + ".", suffix=".tmp",
        dir=signature.parent, delete=False,
    )
    temporary_path = Path(temporary.name)
    temporary.close()
    try:
        result = _run([
            str(openssl), "dgst", "-sha256", "-sign", str(private_key),
            "-sigopt", "rsa_padding_mode:pss",
            "-sigopt", "rsa_pss_saltlen:32",
            "-sigopt", "rsa_mgf1_md:sha256",
            "-out", str(temporary_path), str(artifact),
        ])
        if result.returncode != 0:
            raise ValueError("OpenSSL RSA-PSS signing failed")
        if temporary_path.stat().st_size != policy["signature_size_bytes"]:
            raise ValueError("RSA-PSS signature size is not 256 bytes")
        temporary_path.replace(signature)
    finally:
        temporary_path.unlink(missing_ok=True)
    return _file_sha256(artifact)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--signature", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=Path("openssl"))
    parser.add_argument("--profile", choices=("research", "release"), required=True)
    args = parser.parse_args()
    try:
        digest = sign_artifact(
            args.artifact, args.signature, args.private_key,
            args.policy, args.openssl, args.profile,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"BOOT SIGNATURE FAIL: {error}")
        return 1
    print(f"BOOT SIGNATURE PASS: algorithm={ALGORITHM} artifact_sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
