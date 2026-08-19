#!/usr/bin/env python3
"""Install public headers and reusable objects into a conventional sysroot.

Compilation and archive creation are delegated to the upstream Zig/LLVM
toolchain. This script selects REIST's freestanding target profile and lays out
ordinary ``usr/include`` / ``usr/lib`` SDK artifacts; it implements no custom
compiler, linker or archive format.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from build_user_program import ROOT, find_zig, freestanding_compile_prefix


CORE_ROOT = ROOT / "userspace" / "sdk"
CORE_INCLUDE_ROOT = CORE_ROOT / "include"
GUI_INCLUDE_ROOT = ROOT / "userspace" / "gui" / "include"
PUBLIC_INCLUDE_ROOTS = (CORE_INCLUDE_ROOT, GUI_INCLUDE_ROOT)

CORE_LIBRARY_SOURCES = (
    CORE_ROOT / "x86os.c",
    CORE_ROOT / "reist_dhcp_state.c",
    CORE_ROOT / "reist_dns.c",
)
NETWORK_PARSER_SOURCES = (
    CORE_ROOT / "reist_ipv4_parser.c",
    CORE_ROOT / "reist_icmp_parser.c",
    CORE_ROOT / "reist_udp_parser.c",
    CORE_ROOT / "reist_dhcp_parser.c",
    CORE_ROOT / "reist_tcp_parser.c",
)
GUI_LIBRARY_SOURCES = (
    ROOT / "userspace" / "gui" / "lib" / "menu.c",
    ROOT / "userspace" / "gui" / "lib" / "dialog.c",
    ROOT / "userspace" / "gui" / "lib" / "control.c",
    ROOT / "userspace" / "gui" / "lib" / "container.c",
    ROOT / "userspace" / "gui" / "lib" / "tabs.c",
    ROOT / "userspace" / "gui" / "lib" / "value_controls.c",
    ROOT / "userspace" / "gui" / "lib" / "text_editor.c",
)
STARTUP_SOURCE = CORE_ROOT / "crt0.c"


@dataclass(frozen=True)
class SdkArtifacts:
    """Absolute paths of one complete SDK build."""

    root: Path
    include_dir: Path
    library_dir: Path
    startup_object: Path
    core_library: Path
    network_parser_library: Path
    gui_library: Path


def sdk_artifacts(output: Path) -> SdkArtifacts:
    """Describe conventional output paths without touching the filesystem."""
    root = output.resolve()
    library_dir = root / "usr" / "lib"
    return SdkArtifacts(
        root=root,
        include_dir=root / "usr" / "include",
        library_dir=library_dir,
        startup_object=library_dir / "crt0.o",
        core_library=library_dir / "libreistos.a",
        network_parser_library=library_dir / "libreistnetparse.a",
        gui_library=library_dir / "libreistgui.a",
    )


def write_if_changed(path: Path, content: str) -> None:
    """Write generated ASCII metadata only when its content changed."""
    if path.is_file() and path.read_text(encoding="ascii") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="ascii")


def write_pkg_config(library_dir: Path) -> None:
    """Write metadata understood by ordinary pkg-config implementations."""
    common = (
        "prefix=${pcfiledir}/../..\n"
        "includedir=${prefix}/include\n"
        "libdir=${prefix}/lib\n\n"
    )
    write_if_changed(
        library_dir / "pkgconfig/reist-os.pc",
        common +
        "Name: reist-os\n"
        "Description: REIST Ring-3 system API\n"
        "Version: 1.0.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lreistos\n",
    )
    write_if_changed(
        library_dir / "pkgconfig/reist-gui.pc",
        common +
        "Name: reist-gui\n"
        "Description: Fixed-capacity REIST Ring-3 GUI components\n"
        "Version: 1.0.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lreistgui\n",
    )


def run(command: list[str], environment: dict[str, str]) -> None:
    """Run one upstream tool in the repository working directory."""
    subprocess.run(command, check=True, cwd=ROOT, env=environment)


def compile_objects(
    sources: tuple[Path, ...], prefix: list[str], temporary: Path,
    stem: str, environment: dict[str, str],
) -> list[Path]:
    """Compile a fixed source tuple into temporary ELF32 objects."""
    objects: list[Path] = []
    for index, source in enumerate(sources):
        object_path = temporary / f"{stem}-{index}.o"
        run(
            [*prefix, "-std=c11", "-c", str(source),
             "-o", str(object_path)],
            environment,
        )
        objects.append(object_path)
    return objects


def create_archive(
    zig: Path, destination: Path, objects: list[Path], temporary: Path,
    environment: dict[str, str],
) -> None:
    """Create and atomically publish one conventional static archive."""
    archive = temporary / destination.name
    run(
        [str(zig), "ar", "rcs", str(archive),
         *(str(value) for value in objects)],
        environment,
    )
    shutil.copy2(archive, destination)


def artifact_requires_rebuild(
    artifact: Path, dependencies: tuple[Path, ...], incremental: bool,
) -> bool:
    """Return whether one independently versioned SDK artifact is stale."""
    if not incremental or not artifact.is_file():
        return True
    artifact_time = artifact.stat().st_mtime_ns
    return any(dependency.stat().st_mtime_ns > artifact_time
               for dependency in dependencies)


def build_sdk(output: Path, zig: Path, incremental: bool = False,
              cache_directory: Path | None = None) -> SdkArtifacts:
    """Build headers, startup object and reusable static libraries once."""
    artifacts = sdk_artifacts(output)
    public_headers = tuple(
        (root, header)
        for root in PUBLIC_INCLUDE_ROOTS
        for header in root.rglob("*.h")
    )
    core_headers = tuple(
        header for root, header in public_headers
        if root == CORE_INCLUDE_ROOT)
    gui_headers = tuple(
        header for root, header in public_headers
        if root == GUI_INCLUDE_ROOT)
    all_sources = (
        STARTUP_SOURCE, *CORE_LIBRARY_SOURCES,
        *NETWORK_PARSER_SOURCES, *GUI_LIBRARY_SOURCES,
    )
    if not core_headers or not gui_headers or any(
            not source.is_file()
            for source in (*all_sources, *core_headers, *gui_headers)):
        raise FileNotFoundError("REIST SDK sources are incomplete")

    for root, header in public_headers:
        destination = artifacts.include_dir / header.relative_to(root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if (not destination.is_file() or
                destination.read_bytes() != header.read_bytes()):
            shutil.copy2(header, destination)
    artifacts.library_dir.mkdir(parents=True, exist_ok=True)
    write_pkg_config(artifacts.library_dir)

    startup_stale = artifact_requires_rebuild(
        artifacts.startup_object,
        (STARTUP_SOURCE, *core_headers), incremental)
    core_stale = artifact_requires_rebuild(
        artifacts.core_library,
        (*CORE_LIBRARY_SOURCES, *core_headers), incremental)
    parser_stale = artifact_requires_rebuild(
        artifacts.network_parser_library,
        (*NETWORK_PARSER_SOURCES, *core_headers), incremental)
    gui_stale = artifact_requires_rebuild(
        artifacts.gui_library,
        (*GUI_LIBRARY_SOURCES, *core_headers, *gui_headers), incremental)
    if not (startup_stale or core_stale or parser_stale or gui_stale):
        return artifacts

    with tempfile.TemporaryDirectory(prefix="reist-user-sdk-") as temporary:
        temporary_path = Path(temporary)
        cache_root = cache_directory.resolve() \
            if cache_directory is not None else temporary_path
        cache_root.mkdir(parents=True, exist_ok=True)
        environment = os.environ.copy()
        environment["ZIG_GLOBAL_CACHE_DIR"] = str(
            cache_root / "zig-global")
        environment["ZIG_LOCAL_CACHE_DIR"] = str(
            temporary_path / "zig-local")
        prefix = freestanding_compile_prefix(zig, [GUI_INCLUDE_ROOT])

        if startup_stale:
            startup = compile_objects(
                (STARTUP_SOURCE,), prefix, temporary_path, "startup",
                environment)[0]
            shutil.copy2(startup, artifacts.startup_object)
        if core_stale:
            core_objects = compile_objects(
                CORE_LIBRARY_SOURCES, prefix, temporary_path, "core",
                environment)
            create_archive(
                zig, artifacts.core_library, core_objects,
                temporary_path, environment)
        if parser_stale:
            parser_objects = compile_objects(
                NETWORK_PARSER_SOURCES, prefix, temporary_path, "parser",
                environment)
            create_archive(
                zig, artifacts.network_parser_library, parser_objects,
                temporary_path, environment)
        if gui_stale:
            gui_objects = compile_objects(
                GUI_LIBRARY_SOURCES, prefix, temporary_path, "gui",
                environment)
            create_archive(
                zig, artifacts.gui_library, gui_objects,
                temporary_path, environment)
    return artifacts


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--zig", type=Path)
    parser.add_argument("--incremental", action="store_true")
    args = parser.parse_args()
    artifacts = build_sdk(
        args.output_dir, find_zig(args.zig), args.incremental)
    print(f"REIST SDK include: {artifacts.include_dir}")
    print(f"REIST SDK startup: {artifacts.startup_object}")
    print(f"REIST SDK core library: {artifacts.core_library}")
    print(f"REIST SDK parser library: {artifacts.network_parser_library}")
    print(f"REIST SDK GUI library: {artifacts.gui_library}")


if __name__ == "__main__":
    main()
