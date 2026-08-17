#!/usr/bin/env python3
"""Bounded lowercase FAT 8.3 tree used by REIST boot-image builders."""

from __future__ import annotations

from dataclasses import dataclass, field
from collections.abc import Mapping


MAX_TREE_DEPTH = 4
MAX_DIRECTORY_ENTRIES = 224


@dataclass
class FatFile:
    name: str
    short_name: bytes
    nt_case: int
    contents: bytes
    clusters: list[int] = field(default_factory=list)


@dataclass
class FatDirectory:
    name: str
    short_name: bytes = b""
    nt_case: int = 0
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


def nt_case_flags(component: str) -> int:
    parts = component.split(".")
    flags = 0x08 if any("a" <= value <= "z" for value in parts[0]) else 0
    if len(parts) == 2 and any("a" <= value <= "z" for value in parts[1]):
        flags |= 0x10
    return flags


def _entry_count(directory: FatDirectory) -> int:
    return len(directory.directories) + len(directory.files)


def build_tree(data_files: Mapping[str, bytes]) -> FatDirectory:
    root = FatDirectory("")
    for path, raw_contents in data_files.items():
        if not isinstance(path, str) or not path or "\\" in path or path[0] == "/":
            raise ValueError(f"invalid relative FAT path: {path!r}")
        components = path.split("/")
        if (len(components) > MAX_TREE_DEPTH or any(
                not component or component in (".", "..")
                for component in components)):
            raise ValueError(f"invalid or excessive FAT path depth: {path!r}")
        directory = root
        for component in components[:-1]:
            encoded = short_name(component)
            if any(item.short_name == encoded for item in directory.files):
                raise ValueError(f"FAT path collides with file: {path!r}")
            child = next((item for item in directory.directories
                          if item.short_name == encoded), None)
            if child is None:
                if _entry_count(directory) >= MAX_DIRECTORY_ENTRIES:
                    raise ValueError("FAT directory entry limit exceeded")
                child = FatDirectory(component, encoded,
                                     nt_case_flags(component), directory)
                directory.directories.append(child)
            directory = child
        leaf = components[-1]
        encoded = short_name(leaf)
        if (any(item.short_name == encoded for item in directory.directories) or
                any(item.short_name == encoded for item in directory.files)):
            raise ValueError(f"duplicate FAT path: {path!r}")
        if _entry_count(directory) >= MAX_DIRECTORY_ENTRIES:
            raise ValueError("FAT directory entry limit exceeded")
        if not isinstance(raw_contents, (bytes, bytearray, memoryview)):
            raise TypeError(f"contents for {path!r} must be bytes-like")
        contents = bytes(raw_contents)
        if len(contents) > 0xFFFFFFFF:
            raise ValueError(f"file is too large for FAT: {path!r}")
        directory.files.append(FatFile(leaf, encoded, nt_case_flags(leaf),
                                       contents))
    return root


def walk_directories(root: FatDirectory) -> list[FatDirectory]:
    result: list[FatDirectory] = []

    def visit(directory: FatDirectory) -> None:
        result.append(directory)
        for child in directory.directories:
            visit(child)

    visit(root)
    return result
