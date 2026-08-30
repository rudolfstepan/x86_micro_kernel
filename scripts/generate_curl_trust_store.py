#!/usr/bin/env python3
"""Generate the bounded embedded REIST TLS trust store from pinned PEM."""

from __future__ import annotations

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/cacert-2026-08-13.pem"
OUTPUT = ROOT / "userspace/tls/lib/reist_tls_trust_anchors.c"
TEST_CA = ROOT / "test/fixtures/curl_tls_ca.pem"
EXPECTED_SHA256 = "f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"
EXPECTED_TEST_CA_SHA256 = "5b695ccafbcb03fceeaf7a11a8154c9626630880b73f803a060b0534767f5dd1"
EXPECTED_CERTIFICATES = 121


def append_array(lines: list[str], name: str, data: bytes) -> None:
    lines.append(f"const uint8_t {name}[] =")
    for offset in range(0, len(data), 32):
        escaped = "".join(f"\\x{value:02x}" for value in data[offset:offset + 32])
        lines.append(f'    "{escaped}"')
    lines.extend([
        "    ;", "",
        f"const size_t {name}_size = sizeof({name});", "",
    ])


def main() -> int:
    data = SOURCE.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise ValueError("CA bundle SHA-256 mismatch")
    if data.count(b"-----BEGIN CERTIFICATE-----") != EXPECTED_CERTIFICATES or \
            data.count(b"-----END CERTIFICATE-----") != EXPECTED_CERTIFICATES:
        raise ValueError("CA bundle certificate count mismatch")
    test_ca = TEST_CA.read_bytes()
    if hashlib.sha256(test_ca).hexdigest() != EXPECTED_TEST_CA_SHA256:
        raise ValueError("runtime-test CA SHA-256 mismatch")
    lines = [
        '#include "reist_tls_trust_anchors.h"', "",
        "/* Generated from curl/Mozilla cacert-2026-08-13.pem.",
        f" * SHA-256: {EXPECTED_SHA256}; certificates: {EXPECTED_CERTIFICATES}. */",
    ]
    append_array(lines, "reist_tls_trust_anchors_pem", data)
    lines.extend([
        "/* Test-only root, never selected by the production curl build. */",
        f"/* SHA-256: {EXPECTED_TEST_CA_SHA256}. */",
    ])
    append_array(lines, "reist_tls_runtime_test_ca_pem", test_ca)
    OUTPUT.write_text("\n".join(lines), encoding="ascii", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
