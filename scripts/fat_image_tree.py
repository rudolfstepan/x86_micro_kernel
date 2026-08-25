#!/usr/bin/env python3
"""Bounded lowercase VFAT tree used by REIST boot-image builders."""

from __future__ import annotations

from dataclasses import dataclass, field
from collections.abc import Iterable, Mapping


MAX_TREE_DEPTH = 4
MAX_DIRECTORY_ENTRIES = 224


@dataclass
class FatFile:
    name: str
    short_name: bytes
    nt_case: int
    long_name: bool
    contents: bytes
    clusters: list[int] = field(default_factory=list)


@dataclass
class FatDirectory:
    name: str
    short_name: bytes = b""
    nt_case: int = 0
    long_name: bool = False
    parent: "FatDirectory | None" = None
    directories: list["FatDirectory"] = field(default_factory=list)
    files: list[FatFile] = field(default_factory=list)
    clusters: list[int] = field(default_factory=list)


def short_name(component: str) -> bytes:
    if not component or not component.isascii() or component != component.lower():
        raise ValueError(f"FAT path component must be lowercase ASCII: {component!r}")
    parts = component.split(".")
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8:
        raise ValueError(f"invalid FAT 8.3 filename: {component!r}")
    extension = parts[1] if len(parts) == 2 else ""
    if (len(parts) == 2 and not extension) or len(extension) > 3:
        raise ValueError(f"invalid FAT 8.3 filename: {component!r}")
    allowed = "!#$%&'()-@^_`{}~"
    for part in (parts[0], extension):
        if any(not value.isalnum() and value not in allowed for value in part):
            raise ValueError(f"invalid FAT filename character: {component!r}")
    return (parts[0].upper().ljust(8) + extension.upper().ljust(3)).encode(
        "ascii"
    )


def validate_component(component: str) -> None:
    if (not component or not component.isascii() or
            component != component.lower() or len(component) > 255 or
            component[-1] in " ." or
            any(ord(value) < 0x20 or value in '\\"*/:<>?|' for value in component)):
        raise ValueError(f"invalid lowercase VFAT filename: {component!r}")


def disk_name(component: str, used: set[bytes]) -> tuple[bytes, bool]:
    validate_component(component)
    try:
        encoded = short_name(component)
        if encoded not in used:
            return encoded, False
    except ValueError:
        pass
    stem, dot, extension = component.rpartition(".")
    if not dot or not stem:
        stem, extension = component, ""
    clean_stem = "".join(value for value in stem if value.isalnum()) or "_"
    clean_extension = "".join(value for value in extension if value.isalnum())[:3]
    for sequence in range(1, 1_000_000):
        suffix = f"~{sequence}"
        base = (clean_stem[:8 - len(suffix)] + suffix).upper().ljust(8)
        encoded = (base + clean_extension.upper().ljust(3)).encode("ascii")
        if encoded not in used:
            return encoded, True
    raise ValueError("FAT short-name alias space exhausted")


def nt_case_flags(component: str) -> int:
    parts = component.split(".")
    flags = 0x08 if any("a" <= value <= "z" for value in parts[0]) else 0
    if len(parts) == 2 and any("a" <= value <= "z" for value in parts[1]):
        flags |= 0x10
    return flags


def _entry_count(directory: FatDirectory) -> int:
    return sum(1 + ((len(item.name) + 12) // 13 if item.long_name else 0)
               for item in [*directory.directories, *directory.files])


def _path_components(path: str) -> list[str]:
    if not isinstance(path, str) or not path or "\\" in path or path[0] == "/":
        raise ValueError(f"invalid relative FAT path: {path!r}")
    components = path.split("/")
    if (len(components) > MAX_TREE_DEPTH or any(
            not component or component in (".", "..")
            for component in components)):
        raise ValueError(f"invalid or excessive FAT path depth: {path!r}")
    return components


def _directory_child(directory: FatDirectory, component: str,
                     path: str) -> FatDirectory:
    validate_component(component)
    if any(item.name.lower() == component.lower() for item in directory.files):
        raise ValueError(f"FAT path collides with file: {path!r}")
    child = next((item for item in directory.directories
                  if item.name.lower() == component.lower()), None)
    if child is not None:
        return child
    used = {item.short_name for item in
            [*directory.directories, *directory.files]}
    encoded, long_name = disk_name(component, used)
    slots = 1 + ((len(component) + 12) // 13 if long_name else 0)
    if _entry_count(directory) + slots > MAX_DIRECTORY_ENTRIES:
        raise ValueError("FAT directory entry limit exceeded")
    child = FatDirectory(component, encoded, nt_case_flags(component),
                         long_name, directory)
    directory.directories.append(child)
    return child


def build_tree(data_files: Mapping[str, bytes],
               data_directories: Iterable[str] = ()) -> FatDirectory:
    root = FatDirectory("")
    if isinstance(data_directories, str):
        raise TypeError("FAT directory paths must be an iterable of strings")
    for path in data_directories:
        directory = root
        for component in _path_components(path):
            directory = _directory_child(directory, component, path)
    for path, raw_contents in data_files.items():
        components = _path_components(path)
        directory = root
        for component in components[:-1]:
            directory = _directory_child(directory, component, path)
        leaf = components[-1]
        validate_component(leaf)
        if any(item.name.lower() == leaf.lower()
               for item in directory.directories):
            raise ValueError(f"FAT path collides with directory: {path!r}")
        if any(item.name.lower() == leaf.lower() for item in directory.files):
            raise ValueError(f"duplicate FAT path: {path!r}")
        if _entry_count(directory) >= MAX_DIRECTORY_ENTRIES:
            raise ValueError("FAT directory entry limit exceeded")
        if not isinstance(raw_contents, (bytes, bytearray, memoryview)):
            raise TypeError(f"contents for {path!r} must be bytes-like")
        contents = bytes(raw_contents)
        if len(contents) > 0xFFFFFFFF:
            raise ValueError(f"file is too large for FAT: {path!r}")
        used = {item.short_name for item in
                [*directory.directories, *directory.files]}
        encoded, long_name = disk_name(leaf, used)
        slots = 1 + ((len(leaf) + 12) // 13 if long_name else 0)
        if _entry_count(directory) + slots > MAX_DIRECTORY_ENTRIES:
            raise ValueError("FAT directory entry limit exceeded")
        directory.files.append(FatFile(leaf, encoded, nt_case_flags(leaf),
                                       long_name, contents))
    return root


def walk_directories(root: FatDirectory) -> list[FatDirectory]:
    result: list[FatDirectory] = []

    def visit(directory: FatDirectory) -> None:
        result.append(directory)
        for child in directory.directories:
            visit(child)

    visit(root)
    return result
