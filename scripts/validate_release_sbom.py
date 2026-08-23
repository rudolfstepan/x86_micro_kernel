#!/usr/bin/env python3
"""Independently validate the bounded REIST SPDX 2.3 binary SBOM."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath


MAX_FILES = 160
MAX_FILE_SIZE = 128 * 1024 * 1024
MAX_TOTAL_SIZE = 512 * 1024 * 1024
MAX_DOCUMENT_SIZE = 2 * 1024 * 1024
TOP_KEYS = {
    "spdxVersion", "dataLicense", "SPDXID", "name", "documentNamespace",
    "creationInfo", "packages", "files", "relationships",
}


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


def _object_without_duplicates(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def _checksums(path: Path) -> tuple[str, str]:
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            sha1.update(chunk)
            sha256.update(chunk)
    return sha1.hexdigest(), sha256.hexdigest()


def validate_sbom(sbom: Path, root: Path) -> int:
    root = root.resolve()
    build_root = _absolute_lexical(root / "build")
    sbom_path = _absolute_lexical(sbom if sbom.is_absolute() else root / sbom)
    try:
        sbom_path.relative_to(build_root)
    except ValueError as error:
        raise ValueError("SPDX document escapes build root") from error
    if _has_symlink_component(sbom_path, build_root) or not sbom_path.is_file() or \
            sbom_path.stat().st_size > MAX_DOCUMENT_SIZE:
        raise ValueError("SPDX document is missing or exceeds 2 MiB")
    document = json.loads(
        sbom_path.read_text(encoding="utf-8"),
        object_pairs_hook=_object_without_duplicates,
        parse_constant=lambda value: (_ for _ in ()).throw(
            ValueError(f"invalid JSON constant: {value}")
        ),
    )
    if not isinstance(document, dict) or set(document) != TOP_KEYS:
        raise ValueError("SPDX document fields are not the closed REIST subset")
    if (document["spdxVersion"], document["dataLicense"], document["SPDXID"]) != \
            ("SPDX-2.3", "CC0-1.0", "SPDXRef-DOCUMENT"):
        raise ValueError("SPDX document identity is unsupported")
    if document["name"] != "REIST OS binary release SBOM":
        raise ValueError("SPDX document name is unsupported")
    creation = document["creationInfo"]
    if not isinstance(creation, dict) or set(creation) != {"created", "creators"} or \
            creation["creators"] != ["Tool: REIST generate_release_sbom.py"] or \
            not isinstance(creation["created"], str) or \
            re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", creation["created"]) is None:
        raise ValueError("SPDX creation information is invalid")
    packages = document["packages"]
    if not isinstance(packages, list) or len(packages) != 1:
        raise ValueError("SPDX must describe exactly one package")
    package = packages[0]
    required_package = {
        "SPDXID": "SPDXRef-Package-REIST-OS", "name": "REIST OS",
        "downloadLocation": "NOASSERTION", "filesAnalyzed": True,
        "licenseConcluded": "NOASSERTION", "licenseDeclared": "NOASSERTION",
        "copyrightText": "NOASSERTION",
    }
    if not isinstance(package, dict) or any(package.get(k) != v for k, v in required_package.items()) or \
            set(package) != set(required_package) | {
                "checksums", "packageVerificationCode",
            }:
        raise ValueError("SPDX package fields are invalid")

    files = document["files"]
    if not isinstance(files, list) or not 1 <= len(files) <= MAX_FILES:
        raise ValueError("SPDX file count is outside [1, 160]")
    aggregate = hashlib.sha256()
    identifiers = set()
    names = set()
    expected_contains = set()
    total_size = 0
    ordered_names = []
    file_sha1s = []
    file_keys = {
        "SPDXID", "fileName", "fileTypes", "checksums", "licenseConcluded",
        "licenseInfoInFiles", "copyrightText", "comment",
    }
    for index, entry in enumerate(files):
        if not isinstance(entry, dict) or set(entry) != file_keys:
            raise ValueError("SPDX file entry is not an object")
        name = entry.get("fileName")
        identifier = entry.get("SPDXID")
        comment = entry.get("comment")
        size_match = re.fullmatch(r"REIST-Artifact-Size: ([0-9]+) bytes", comment) \
            if isinstance(comment, str) else None
        size = int(size_match.group(1)) if size_match is not None else None
        checksums = entry.get("checksums")
        if not isinstance(name, str) or not name.startswith("./build/") or \
                PurePosixPath(name).is_absolute() or ".." in PurePosixPath(name).parts:
            raise ValueError("SPDX file path is not canonical build-relative")
        if not isinstance(identifier, str) or name.casefold() in names or \
                identifier in identifiers:
            raise ValueError("SPDX file path or identifier is duplicate")
        names.add(name.casefold()); identifiers.add(identifier); ordered_names.append(name)
        if not isinstance(size, int) or size < 0 or size > MAX_FILE_SIZE or \
                entry.get("fileTypes") != ["BINARY"] or \
                entry.get("licenseConcluded") != "NOASSERTION" or \
                entry.get("licenseInfoInFiles") != ["NOASSERTION"] or \
                entry.get("copyrightText") != "NOASSERTION" or \
                not isinstance(checksums, list) or len(checksums) != 2 or \
                not all(isinstance(item, dict) and
                        set(item) == {"algorithm", "checksumValue"}
                        for item in checksums) or \
                checksums[0].get("algorithm") != "SHA1" or \
                re.fullmatch(r"[0-9a-f]{40}", checksums[0].get("checksumValue", "")) is None or \
                checksums[1].get("algorithm") != "SHA256" or \
                re.fullmatch(r"[0-9a-f]{64}", checksums[1].get("checksumValue", "")) is None:
            raise ValueError("SPDX file size or SHA-256 is invalid")
        path = _absolute_lexical(root / name[2:])
        try:
            path.relative_to(build_root)
        except ValueError as error:
            raise ValueError("SPDX file escapes build root") from error
        if _has_symlink_component(path, build_root) or not path.is_file() or \
                path == sbom_path:
            raise ValueError(f"SPDX live artifact drift: {name}")
        live_sha1, live_sha256 = _checksums(path)
        if path.stat().st_size != size or live_sha1 != checksums[0]["checksumValue"] or \
                live_sha256 != checksums[1]["checksumValue"]:
            raise ValueError(f"SPDX live artifact drift: {name}")
        if identifier != f"SPDXRef-File-{index:03d}-{live_sha256[:12]}":
            raise ValueError("SPDX file identifier is invalid")
        total_size += size
        if total_size > MAX_TOTAL_SIZE:
            raise ValueError("SPDX live artifacts exceed 512 MiB")
        file_sha1s.append(checksums[0]["checksumValue"])
        digest = checksums[1]["checksumValue"]
        aggregate.update(name.encode("utf-8")); aggregate.update(b"\0" + str(size).encode("ascii") + b"\0"); aggregate.update(bytes.fromhex(digest))
        expected_contains.add(("SPDXRef-Package-REIST-OS", "CONTAINS", identifier))
    if ordered_names != sorted(ordered_names, key=str.casefold):
        raise ValueError("SPDX files are not canonically ordered")
    artifact_digest = aggregate.hexdigest()
    if document["documentNamespace"] != f"https://reist.invalid/spdx/reist-os/{artifact_digest}" or \
            package["checksums"] != [{"algorithm": "SHA256", "checksumValue": artifact_digest}]:
        raise ValueError("SPDX artifact-set binding is invalid")
    verification = hashlib.sha1(
        "".join(sorted(file_sha1s)).encode("ascii")
    ).hexdigest()
    if package["packageVerificationCode"] != {
            "packageVerificationCodeValue": verification}:
        raise ValueError("SPDX package verification code is invalid")
    relationships = document["relationships"]
    relationship_keys = {
        "spdxElementId", "relationshipType", "relatedSpdxElement",
    }
    if not isinstance(relationships, list) or any(
            not isinstance(item, dict) or set(item) != relationship_keys or
            not all(isinstance(value, str) for value in item.values())
            for item in relationships):
        raise ValueError("SPDX relationships are malformed")
    actual = {(r["spdxElementId"], r["relationshipType"], r["relatedSpdxElement"]) for r in relationships}
    expected = {("SPDXRef-DOCUMENT", "DESCRIBES", "SPDXRef-Package-REIST-OS")} | expected_contains
    if len(relationships) != len(expected) or actual != expected:
        raise ValueError("SPDX relationships are incomplete or ambiguous")
    return len(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sbom", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    try:
        count = validate_sbom(args.sbom, args.root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"RELEASE_SBOM_VALIDATE FAIL: {error}")
        return 1
    print(f"RELEASE_SBOM_VALIDATE PASS files={count} sbom={args.sbom}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
