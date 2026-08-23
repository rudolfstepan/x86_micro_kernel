#!/usr/bin/env python3
"""Generate a bounded SPDX 2.3 JSON SBOM for REIST binary artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path


MAX_FILES = 160
MAX_FILE_SIZE = 128 * 1024 * 1024
MAX_TOTAL_SIZE = 512 * 1024 * 1024
MAX_DOCUMENT_SIZE = 2 * 1024 * 1024


def _absolute_lexical(path: Path) -> Path:
    return Path(os.path.abspath(path))


def _has_symlink_component(path: Path, boundary: Path) -> bool:
    current = path
    while True:
        if current.is_symlink():
            return True
        if current == boundary:
            return False
        if current.parent == current:
            return True
        current = current.parent


def _checksums(path: Path) -> tuple[str, str]:
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            sha1.update(chunk)
            sha256.update(chunk)
    return sha1.hexdigest(), sha256.hexdigest()


def _artifact(root: Path, build_root: Path, value: Path) -> tuple[str, Path]:
    path = _absolute_lexical(value if value.is_absolute() else root / value)
    try:
        relative = path.relative_to(build_root)
    except ValueError as error:
        raise ValueError(f"SBOM artifact escapes build root: {value}") from error
    if _has_symlink_component(path, build_root) or not path.is_file():
        raise ValueError(f"SBOM artifact is not a regular non-symlink file: {value}")
    canonical = "./build/" + relative.as_posix()
    return canonical, path


def generate_sbom(
        root: Path, output: Path, artifacts: list[Path],
        program_dir: Path | None = None) -> dict:
    root = root.resolve()
    build_root = _absolute_lexical(root / "build")
    output_path = _absolute_lexical(output if output.is_absolute() else root / output)
    try:
        output_path.relative_to(build_root)
    except ValueError as error:
        raise ValueError("SBOM output must remain under build root") from error
    if _has_symlink_component(output_path, build_root):
        raise ValueError("SBOM output path must not contain symlinks")

    candidates = list(artifacts)
    if program_dir is not None:
        directory = _absolute_lexical(
            program_dir if program_dir.is_absolute() else root / program_dir
        )
        if _has_symlink_component(directory, build_root) or \
                not directory.is_dir() or directory.parent != build_root:
            raise ValueError("SBOM program directory must be directly under build")
        candidates.extend(sorted(directory.glob("*.PRG"), key=lambda p: p.name))
    if not candidates or len(candidates) > MAX_FILES:
        raise ValueError("SBOM artifact count is outside [1, 160]")

    collected = []
    total_size = 0
    seen = set()
    for value in candidates:
        canonical, path = _artifact(root, build_root, value)
        key = canonical.casefold()
        if key in seen or path == output_path:
            raise ValueError("SBOM artifacts are duplicate or include the output")
        seen.add(key)
        size = path.stat().st_size
        if size < 0 or size > MAX_FILE_SIZE:
            raise ValueError(f"SBOM artifact exceeds 128 MiB: {canonical}")
        total_size += size
        if total_size > MAX_TOTAL_SIZE:
            raise ValueError("SBOM artifacts exceed 512 MiB total")
        sha1, sha256 = _checksums(path)
        collected.append((canonical, size, sha1, sha256))
    collected.sort(key=lambda item: item[0].casefold())

    aggregate = hashlib.sha256()
    files = []
    relationships = [{
        "spdxElementId": "SPDXRef-DOCUMENT",
        "relationshipType": "DESCRIBES",
        "relatedSpdxElement": "SPDXRef-Package-REIST-OS",
    }]
    file_sha1s = []
    for index, (name, size, sha1, sha256) in enumerate(collected):
        aggregate.update(name.encode("utf-8"))
        aggregate.update(b"\0" + str(size).encode("ascii") + b"\0")
        aggregate.update(bytes.fromhex(sha256))
        file_sha1s.append(sha1)
        identifier = f"SPDXRef-File-{index:03d}-{sha256[:12]}"
        files.append({
            "SPDXID": identifier,
            "fileName": name,
            "fileTypes": ["BINARY"],
            "checksums": [
                {"algorithm": "SHA1", "checksumValue": sha1},
                {"algorithm": "SHA256", "checksumValue": sha256},
            ],
            "licenseConcluded": "NOASSERTION",
            "licenseInfoInFiles": ["NOASSERTION"],
            "copyrightText": "NOASSERTION",
            "comment": f"REIST-Artifact-Size: {size} bytes",
        })
        relationships.append({
            "spdxElementId": "SPDXRef-Package-REIST-OS",
            "relationshipType": "CONTAINS",
            "relatedSpdxElement": identifier,
        })
    artifact_digest = aggregate.hexdigest()
    verification = hashlib.sha1(
        "".join(sorted(file_sha1s)).encode("ascii")
    ).hexdigest()
    created = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "REIST OS binary release SBOM",
        "documentNamespace": f"https://reist.invalid/spdx/reist-os/{artifact_digest}",
        "creationInfo": {
            "created": created,
            "creators": ["Tool: REIST generate_release_sbom.py"],
        },
        "packages": [{
            "SPDXID": "SPDXRef-Package-REIST-OS",
            "name": "REIST OS",
            "downloadLocation": "NOASSERTION",
            "filesAnalyzed": True,
            "licenseConcluded": "NOASSERTION",
            "licenseDeclared": "NOASSERTION",
            "copyrightText": "NOASSERTION",
            "packageVerificationCode": {
                "packageVerificationCodeValue": verification,
            },
            "checksums": [{
                "algorithm": "SHA256", "checksumValue": artifact_digest,
            }],
        }],
        "files": files,
        "relationships": relationships,
    }
    encoded = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if len(encoded) > MAX_DOCUMENT_SIZE:
        raise ValueError("SPDX document exceeds 2 MiB")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", suffix=".tmp",
        dir=output_path.parent,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            if stream.write(encoded) != len(encoded):
                raise OSError("short SPDX write")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output_path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--artifact", type=Path, action="append", default=[])
    parser.add_argument("--program-dir", type=Path)
    args = parser.parse_args()
    try:
        document = generate_sbom(
            args.root, args.output, args.artifact, args.program_dir
        )
    except (OSError, ValueError) as error:
        print(f"RELEASE_SBOM_GENERATE FAIL: {error}")
        return 1
    print(f"RELEASE_SBOM_GENERATE PASS files={len(document['files'])} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
