#!/usr/bin/env python3
"""Build the standard Ring-3 system programs into MYPR images."""

from __future__ import annotations

import argparse
import os
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from build_user_program import ROOT, build, find_zig
from build_user_sdk import (
    AUDIO_INCLUDE_ROOT, CONFIG_INCLUDE_ROOT, CORE_INCLUDE_ROOT, GUI_INCLUDE_ROOT,
    IMAGE_INCLUDE_ROOT, TLS_INCLUDE_ROOT, build_sdk,
)


PROGRAMS = {
    "HELLO.PRG": ROOT / "userspace/programs/hello.c",
    "SYSINFO.PRG": ROOT / "userspace/programs/sysinfo.c",
    "USBINFO.PRG": ROOT / "userspace/programs/usbinfo.c",
    "DMESG.PRG": ROOT / "userspace/programs/dmesg.c",
    "REPEAT.PRG": ROOT / "userspace/programs/repeat.c",
    "CALC.PRG": ROOT / "userspace/programs/calc.c",
    "DATE.PRG": ROOT / "userspace/programs/date.c",
    "UPTIME.PRG": ROOT / "userspace/programs/uptime.c",
    "MEMINFO.PRG": ROOT / "userspace/programs/meminfo.c",
    "BENCHMARK.PRG": ROOT / "userspace/programs/benchmark.c",
    "ASCII.PRG": ROOT / "userspace/programs/ascii.c",
    "CAT.PRG": (
        ROOT / "userspace/programs/cat.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "CHKDSK.PRG": ROOT / "userspace/programs/chkdsk.c",
    "FDISK.PRG": ROOT / "userspace/programs/fdisk.c",
    "FORMAT.PRG": ROOT / "userspace/programs/format.c",
    "LS.PRG": (
        ROOT / "userspace/programs/ls.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "SAVE.PRG": ROOT / "userspace/programs/save.c",
    "BASIC.PRG": ROOT / "userspace/bin/basic.c",
    "SPAWN.PRG": ROOT / "userspace/programs/spawn.c",
    "PS.PRG": ROOT / "userspace/programs/ps.c",
    "KILL.PRG": ROOT / "userspace/programs/kill.c",
    "PWD.PRG": ROOT / "userspace/programs/pwd.c",
    "SHELL.PRG": (
        ROOT / "userspace/bin/shell.c",
        ROOT / "userspace/bin/shell_vfs.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "DESKTOP.PRG": (
        ROOT / "userspace/gui/compositor/desktop.c",
        ROOT / "userspace/gui/compositor/desktop_splash.s",
        ROOT / "userspace/gui/compositor/desktop_drag.c",
        ROOT / "userspace/gui/compositor/desktop_wm.c",
        ROOT / "userspace/gui/compositor/desktop_explorer.c",
        ROOT / "userspace/gui/compositor/desktop_trash.c",
        ROOT / "userspace/gui/compositor/desktop_filetypes.c",
        ROOT / "userspace/gui/compositor/desktop_surface.c",
        ROOT / "userspace/gui/compositor/desktop_surface_runtime.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "GUIDEMO.PRG": ROOT / "userspace/gui/apps/control_gallery/main.c",
    "NOTEPAD.PRG": (
        ROOT / "userspace/gui/apps/notepad/main.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "SOUNDPLAYER.PRG": ROOT / "userspace/gui/apps/sound_player/main.c",
    "IMAGEVIEWER.PRG": (
        ROOT / "userspace/gui/apps/image_viewer/main.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "SURFACEDEMO.PRG": ROOT / "userspace/gui/apps/surface_demo/main.c",
    "CONTROL.PRG": (
        ROOT / "userspace/gui/apps/control_panel/main.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "CONFIG.PRG": ROOT / "userspace/services/config/config_service.c",
    "MKDIR.PRG": ROOT / "userspace/programs/mkdir.c",
    "RMDIR.PRG": ROOT / "userspace/programs/rmdir.c",
    "DEL.PRG": ROOT / "userspace/programs/del.c",
    "COPY.PRG": ROOT / "userspace/programs/copy.c",
    "RENAME.PRG": ROOT / "userspace/programs/rename.c",
    "STAT.PRG": (
        ROOT / "userspace/programs/stat.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "DF.PRG": ROOT / "userspace/programs/df.c",
    "TOUCH.PRG": ROOT / "userspace/programs/touch.c",
    "TREE.PRG": (
        ROOT / "userspace/programs/tree.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "FIND.PRG": (
        ROOT / "userspace/programs/find.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "RM.PRG": ROOT / "userspace/programs/rm.c",
    "ECHO.PRG": ROOT / "userspace/programs/echo.c",
    "CLS.PRG": ROOT / "userspace/programs/cls.c",
    "DRIVES.PRG": ROOT / "userspace/programs/drives.c",
    "DEVCTL.PRG": ROOT / "userspace/programs/devctl.c",
    "MOUNT.PRG": ROOT / "userspace/programs/mount.c",
    "UMOUNT.PRG": ROOT / "userspace/programs/umount.c",
    "SVCCTL.PRG": ROOT / "userspace/programs/svcctl.c",
    "IFCONFIG.PRG": ROOT / "userspace/programs/ifconfig.c",
    "PING.PRG": ROOT / "userspace/programs/ping.c",
    "NETSTAT.PRG": ROOT / "userspace/programs/netstat.c",
    "UDP.PRG": ROOT / "userspace/programs/udp.c",
    "NSLOOKUP.PRG": ROOT / "userspace/programs/nslookup.c",
    "NC.PRG": ROOT / "userspace/programs/nc.c",
    "HTTPD.PRG": (
        ROOT / "userspace/programs/httpd.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_stat_client.c",
        ROOT / "userspace/storage/lib/vfs_read_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "CURL.PRG": (
        ROOT / "userspace/programs/curl.c",
        ROOT / "userspace/programs/curl_http.c",
    ),
    "EDIT.PRG": ROOT / "userspace/bin/edit.c",
    "CHILDEX.PRG": ROOT / "userspace/programs/child_exit.c",
    "FAULTDE.PRG": ROOT / "userspace/programs/fault_de.c",
    "FAULTUD.PRG": ROOT / "userspace/programs/fault_ud.c",
    "FAULTPF.PRG": ROOT / "userspace/programs/fault_pf.c",
    "FAULTSTK.PRG": ROOT / "userspace/programs/fault_stack.c",
    "GTEST.PRG": (
        ROOT / "userspace/programs/guest_test.c",
        ROOT / "userspace/storage/lib/vfs_file_client.c",
        ROOT / "userspace/storage/lib/vfs_path.c",
    ),
    "REIST.PRG": ROOT / "userspace/programs/reist_probe.c",
    "STORAGE.PRG": (
        ROOT / "userspace/programs/storage_service.c",
        ROOT / "userspace/storage/lib/vfs_shadow_fat32.c",
        ROOT / "userspace/storage/lib/vfs_shadow_ext2.c",
    ),
    "HDA.PRG": ROOT / "userspace/drivers/audio/hda_driver.c",
    "SVGA2D.PRG": ROOT / "userspace/drivers/video/vmware_svga2d.c",
    "NVIDIA.PRG": (
        ROOT / "userspace/drivers/video/nvidia_gk208.c",
        ROOT / "userspace/video/lib/nvidia_gk208_2d.c",
    ),
    "AUDIO.PRG": ROOT / "userspace/services/audio/audio_service.c",
    "AUDIOINFO.PRG": ROOT / "userspace/programs/audioinfo.c",
    "AUDIOTEST.PRG": ROOT / "userspace/programs/audiotest.c",
    "WAVPLAY.PRG": ROOT / "userspace/programs/wavplay.c",
    "SLEEPER.PRG": ROOT / "userspace/programs/sleep_child.c",
    "CAPWAIT.PRG": ROOT / "userspace/programs/capacity_child.c",
    "SATAWR.PRG": ROOT / "userspace/programs/sata_write_test.c",
}

GUI_PROGRAMS = {
    "DESKTOP.PRG", "GUIDEMO.PRG", "NOTEPAD.PRG", "SOUNDPLAYER.PRG",
    "IMAGEVIEWER.PRG", "SURFACEDEMO.PRG", "CONTROL.PRG",
}
IMAGE_PROGRAMS = {"DESKTOP.PRG", "IMAGEVIEWER.PRG"}
NETWORK_PARSER_PROGRAMS = {"REIST.PRG"}
TLS_PROGRAMS = {"CURL.PRG"}
AUDIO_PROGRAMS = {
    "HDA.PRG", "AUDIO.PRG", "AUDIOINFO.PRG", "AUDIOTEST.PRG",
    "WAVPLAY.PRG", "SOUNDPLAYER.PRG",
}
AUDIO_CLIENT_PROGRAMS = {
    "AUDIOINFO.PRG", "AUDIOTEST.PRG", "WAVPLAY.PRG", "SOUNDPLAYER.PRG",
}
STORAGE_INCLUDE_ROOT = ROOT / "userspace/storage/include"
MAX_SYSTEM_BUILD_WORKERS = 8
DEFAULT_SYSTEM_BUILD_WORKERS = min(
    MAX_SYSTEM_BUILD_WORKERS, max(1, os.cpu_count() or 1))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--zig", type=Path)
    parser.add_argument("--incremental", action="store_true")
    parser.add_argument("--curl-tls-runtime-probe", action="store_true")
    parser.add_argument(
        "-j", "--jobs", type=int, default=DEFAULT_SYSTEM_BUILD_WORKERS,
        help=("parallel PRG builds (default: up to 8 logical CPUs; "
              "accepted range: 1..8)"),
    )
    args = parser.parse_args()
    if not 1 <= args.jobs <= MAX_SYSTEM_BUILD_WORKERS:
        parser.error("--jobs must be between 1 and 8")

    zig = find_zig(args.zig)
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reist-system-cache-") as cache:
        cache_directory = Path(cache)
        global_cache_directory = cache_directory / "zig-global-shared"
        sdk = build_sdk(
            output_dir.parent / "sdk", zig, incremental=args.incremental,
            cache_directory=global_cache_directory)
        core_headers = list(CORE_INCLUDE_ROOT.rglob("*.h"))
        storage_headers = list(STORAGE_INCLUDE_ROOT.rglob("*.h"))
        gui_headers = list(GUI_INCLUDE_ROOT.rglob("*.h"))
        audio_headers = list(AUDIO_INCLUDE_ROOT.rglob("*.h"))
        image_headers = list(IMAGE_INCLUDE_ROOT.rglob("*.h"))
        tls_headers = list(TLS_INCLUDE_ROOT.rglob("*.h"))
        config_headers = list(CONFIG_INCLUDE_ROOT.rglob("*.h"))

        def build_one(item: tuple[str, object]) -> str:
            """Build one independent PRG under the shared read-only SDK."""
            name, source = item
            output = output_dir / name
            before = output.stat().st_mtime_ns if output.is_file() else None
            program_incremental = args.incremental
            curl_mode_marker = sdk.root / ".curl-build-mode"
            curl_mode = "runtime-probe" if args.curl_tls_runtime_probe \
                else "production"
            if name == "CURL.PRG":
                program_incremental = args.incremental and \
                    curl_mode_marker.is_file() and \
                    curl_mode_marker.read_text(encoding="ascii") == curl_mode
            sources = list(source) if isinstance(source, tuple) else [source]
            runtime_libraries = [sdk.core_library]
            if name in NETWORK_PARSER_PROGRAMS:
                runtime_libraries.append(sdk.network_parser_library)
            link_libraries = []
            if name in GUI_PROGRAMS:
                link_libraries.append(sdk.gui_library)
            if name in AUDIO_CLIENT_PROGRAMS:
                link_libraries.append(sdk.audio_library)
            if name in IMAGE_PROGRAMS:
                link_libraries.append(sdk.image_library)
            if name in TLS_PROGRAMS:
                link_libraries.append(sdk.tls_library)
            dependency_files = [*core_headers]
            if name in {"DESKTOP.PRG", "CONTROL.PRG", "CONFIG.PRG"}:
                dependency_files.extend(config_headers)
            if name in {"STORAGE.PRG", "STAT.PRG", "HTTPD.PRG", "CAT.PRG",
                        "LS.PRG", "TREE.PRG", "FIND.PRG", "DESKTOP.PRG",
                        "SHELL.PRG", "GTEST.PRG", "IMAGEVIEWER.PRG"}:
                dependency_files.extend(storage_headers)
            if name in GUI_PROGRAMS:
                dependency_files.extend(gui_headers)
            if name in AUDIO_PROGRAMS:
                dependency_files.extend(audio_headers)
            if name in IMAGE_PROGRAMS:
                dependency_files.extend(image_headers)
            if name in TLS_PROGRAMS:
                dependency_files.extend(tls_headers)
            if name == "DESKTOP.PRG":
                dependency_files.append(
                    ROOT / "assets/images/reist-splash.bmp")
            build(
                sources, output, zig, incremental=program_incremental,
                include_dirs=[sdk.include_dir, STORAGE_INCLUDE_ROOT],
                libraries=link_libraries or None,
                runtime_objects=[sdk.startup_object],
                runtime_libraries=runtime_libraries,
                cache_directory=global_cache_directory,
                dependency_files=dependency_files,
                compile_flags=(
                    ["-DREIST_CURL_TLS_RUNTIME_PROBE"]
                    if args.curl_tls_runtime_probe and name == "CURL.PRG"
                    else None),
            )
            if name == "CURL.PRG":
                curl_mode_marker.write_text(curl_mode, encoding="ascii")
            reused = before is not None and \
                output.stat().st_mtime_ns == before
            action = "Reused" if reused else "Built"
            return f"System program ({action}): {output}"

        # Zig's content-addressed global cache is safe to share and avoids
        # rebuilding compiler artifacts for every PRG. build() still creates
        # one isolated local cache per job. executor.map preserves PROGRAMS
        # order for deterministic diagnostics.
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            for message in executor.map(build_one, PROGRAMS.items()):
                print(message)


if __name__ == "__main__":
    main()
