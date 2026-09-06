#!/usr/bin/env python3
"""Install public headers and reusable objects into a conventional sysroot.

Compilation and archive creation are delegated to the upstream Zig/LLVM
toolchain. This script selects REIST's freestanding target profile and lays out
ordinary ``usr/include`` / ``usr/lib`` SDK artifacts; it implements no custom
compiler, linker or archive format.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from build_user_program import (ROOT, find_zig, freestanding_compile_prefix,
                                cpp_compile_flags, validate_cpp_object)


CORE_ROOT = ROOT / "userspace" / "sdk"
CORE_INCLUDE_ROOT = CORE_ROOT / "include"
GUI_INCLUDE_ROOT = ROOT / "userspace" / "gui" / "include"
AUDIO_INCLUDE_ROOT = ROOT / "userspace" / "audio" / "include"
STORAGE_INCLUDE_ROOT = ROOT / "userspace" / "storage" / "include"
STORAGE_LIBRARY_ROOT = ROOT / "userspace" / "storage" / "lib"
IMAGE_INCLUDE_ROOT = ROOT / "userspace" / "image" / "include"
CONFIG_INCLUDE_ROOT = ROOT / "userspace" / "config" / "include"
TLS_ROOT = ROOT / "userspace" / "tls"
TLS_INCLUDE_ROOT = TLS_ROOT / "include"
TLS_LIBRARY_ROOT = TLS_ROOT / "lib"
MBEDTLS_ARCHIVE = ROOT / "third_party" / "mbedtls-4.1.1.tar.bz2"
MBEDTLS_SHA256 = "3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c"
PUBLIC_INCLUDE_ROOTS = (
    CORE_INCLUDE_ROOT, GUI_INCLUDE_ROOT, AUDIO_INCLUDE_ROOT, IMAGE_INCLUDE_ROOT,
    CONFIG_INCLUDE_ROOT, STORAGE_INCLUDE_ROOT, TLS_INCLUDE_ROOT,
)
TLS_WRAPPER_SOURCES = (
    TLS_LIBRARY_ROOT / "reist_tls.c",
    TLS_LIBRARY_ROOT / "reist_tls_platform.c",
    TLS_LIBRARY_ROOT / "reist_tls_trust_anchors.c",
)
MBEDTLS_LIBRARY_NAMES = (
    "mbedtls_config.c", "ssl_ciphersuites.c", "ssl_client.c", "ssl_msg.c",
    "ssl_tls.c", "ssl_tls12_client.c", "ssl_tls13_keys.c",
    "ssl_tls13_client.c", "ssl_tls13_generic.c", "x509.c", "x509_crt.c",
    "x509_oid.c",
)
MBEDTLS_SUPPORT_NAMES = (
    "extras/md.c", "extras/pk.c", "extras/pk_ecc.c", "extras/pk_rsa.c",
    "extras/pk_wrap.c", "extras/pkparse.c", "extras/pkwrite.c",
    "platform/platform_util.c", "utilities/asn1parse.c",
    "utilities/asn1write.c", "utilities/base64.c",
    "utilities/constant_time.c", "utilities/oid.c", "utilities/pem.c",
)
MBEDTLS_CORE_EXCLUDED = {
    "psa_its_file.c", "psa_crypto_storage.c",
}

CORE_LIBRARY_SOURCES = (
    CORE_ROOT / "x86os.c",
    CORE_ROOT / "reist_dhcp_state.c",
    CORE_ROOT / "reist_dns.c",
    ROOT / "userspace" / "config" / "lib" / "config.c",
    STORAGE_LIBRARY_ROOT / "vfs_path.c",
    STORAGE_LIBRARY_ROOT / "vfs_file_client.c",
    STORAGE_LIBRARY_ROOT / "vfs_stat_client.c",
    STORAGE_LIBRARY_ROOT / "vfs_read_client.c",
    STORAGE_LIBRARY_ROOT / "vfs_symlink_client.c",
    STORAGE_LIBRARY_ROOT / "vfs_namespace_client.c",
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
    ROOT / "userspace" / "gui" / "lib" / "file_dialog.c",
    ROOT / "userspace" / "gui" / "lib" / "surface_client.c",
    ROOT / "userspace" / "gui" / "lib" / "control.c",
    ROOT / "userspace" / "gui" / "lib" / "container.c",
    ROOT / "userspace" / "gui" / "lib" / "tabs.c",
    ROOT / "userspace" / "gui" / "lib" / "value_controls.c",
    ROOT / "userspace" / "gui" / "lib" / "text_editor.c",
    ROOT / "userspace" / "gui" / "lib" / "piece_document.c",
    ROOT / "userspace" / "gui" / "lib" / "font.c",
    ROOT / "userspace" / "gui" / "lib" / "font_catalog.c",
)
AUDIO_LIBRARY_SOURCES = (
    ROOT / "userspace" / "audio" / "lib" / "audio.c",
    ROOT / "userspace" / "audio" / "lib" / "audio_wave.c",
    STORAGE_LIBRARY_ROOT / "vfs_file_client.c",
    STORAGE_LIBRARY_ROOT / "vfs_path.c",
)
IMAGE_LIBRARY_SOURCES = (
    ROOT / "userspace" / "image" / "lib" / "image.c",
    ROOT / "userspace" / "image" / "lib" / "image_ico.c",
)
STARTUP_SOURCE = CORE_ROOT / "crt0.c"
LIBC_ROOT = ROOT / "userspace/libc"
LIBC_INCLUDE_ROOT = LIBC_ROOT / "include"
CPP_ROOT = ROOT / "userspace/cpp"
CPP_INCLUDE_ROOT = CPP_ROOT / "include"
LIBC_SOURCES = tuple(LIBC_ROOT / "lib" / name for name in
                     ("heap.c", "bytes.c", "runtime.c", "process_heap.c"))
WAPCAPLET_ARCHIVE = ROOT / "third_party/libwapcaplet.tar.gz"
WAPCAPLET_SHA256 = "9b2aa1dd6d6645f8e992b3697fdbd87f0c0e1da5721fa54ed29b484d13160c5c"


def extract_wapcaplet(destination: Path) -> Path:
    """Extract exactly the verified source/header/license, never archive links.

    The public header includes sys/types.h but uses none of its declarations.
    Drop only that unused include in the generated SDK header: no fake POSIX
    header, API/type change or modification to the archived upstream source.
    """
    if hashlib.sha256(WAPCAPLET_ARCHIVE.read_bytes()).hexdigest() != WAPCAPLET_SHA256:
        raise ValueError("LibWapcaplet archive SHA-256 mismatch")
    members = ("src/libwapcaplet.c", "include/libwapcaplet/libwapcaplet.h", "COPYING")
    with tarfile.open(WAPCAPLET_ARCHIVE, "r:gz") as archive:
        for relative in members:
            member = archive.getmember("libwapcaplet-0.4.3/" + relative)
            if not member.isfile() or member.size > 65536:
                raise ValueError("invalid pinned LibWapcaplet member")
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError("missing pinned LibWapcaplet member")
            data = stream.read()
            if relative.endswith(".h"):
                include = b"#include <sys/types.h>\n"
                if data.count(include) != 1:
                    raise ValueError("unexpected LibWapcaplet header")
                data = data.replace(include, b"")
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    return destination


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
    audio_library: Path
    image_library: Path
    tls_library: Path
    libc_include_dir: Path
    libc_library: Path
    wapcaplet_library: Path

    @property
    def cpp_include_dir(self) -> Path:
        return self.include_dir / "reist/cpp"

    @property
    def cpp_library(self) -> Path:
        return self.library_dir / "libreistcpp.a"


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
        audio_library=library_dir / "libreistaudio.a",
        image_library=library_dir / "libreistimage.a",
        tls_library=library_dir / "libreisttls.a",
        libc_include_dir=root / "usr/include/reist/libc",
        libc_library=library_dir / "libreistc.a",
        wapcaplet_library=library_dir / "libwapcaplet.a",
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
    write_if_changed(
        library_dir / "pkgconfig/reist-audio.pc",
        common +
        "Name: reist-audio\n"
        "Description: Bounded REIST Ring-3 PCM playback API\n"
        "Version: 1.0.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lreistaudio -lreistos\n",
    )
    write_if_changed(
        library_dir / "pkgconfig/reist-image.pc",
        common +
        "Name: reist-image\n"
        "Description: Bounded REIST raster image decoding API\n"
        "Version: 1.0.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lreistimage\n",
    )
    write_if_changed(
        library_dir / "pkgconfig/reist-tls.pc",
        common +
        "Name: reist-tls\n"
        "Description: Bounded authenticated REIST Ring-3 TLS client\n"
        "Version: 1.0.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lreisttls -lreistos\n",
    )


def run(command: list[str], environment: dict[str, str]) -> None:
    """Run one upstream tool in the repository working directory."""
    subprocess.run(command, check=True, cwd=ROOT, env=environment)


def compile_objects(
    sources: tuple[Path, ...], prefix: list[str], temporary: Path,
    stem: str, environment: dict[str, str], extra_flags: list[str] | None = None,
) -> list[Path]:
    """Compile independent objects with four workers and stable archive order.

    Each job owns a unique output. The caller can publish an archive only
    after every compiler succeeds; the pool joins before temporary cleanup.
    """
    def compile_one(item):
        index, source = item
        object_path = temporary / f"{stem}-{index}.o"
        run(
            [*prefix, *(extra_flags or []), "-std=c11", "-c", str(source),
             "-o", str(object_path)],
            environment,
        )
        return object_path
    with ThreadPoolExecutor(max_workers=4) as pool:
        return list(pool.map(compile_one, enumerate(sources)))


def extract_mbedtls(destination: Path) -> Path:
    """Verify and extract only the pinned Mbed TLS build inputs."""
    digest = hashlib.sha256(MBEDTLS_ARCHIVE.read_bytes()).hexdigest()
    if digest != MBEDTLS_SHA256:
        raise ValueError("Mbed TLS archive SHA-256 mismatch")
    prefixes = (
        "mbedtls-4.1.1/include/", "mbedtls-4.1.1/library/",
        "mbedtls-4.1.1/tf-psa-crypto/include/",
        "mbedtls-4.1.1/tf-psa-crypto/core/",
        "mbedtls-4.1.1/tf-psa-crypto/drivers/builtin/",
        "mbedtls-4.1.1/tf-psa-crypto/extras/",
        "mbedtls-4.1.1/tf-psa-crypto/utilities/",
        "mbedtls-4.1.1/tf-psa-crypto/dispatch/",
        "mbedtls-4.1.1/tf-psa-crypto/platform/",
    )
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(MBEDTLS_ARCHIVE, "r:bz2") as archive:
        members = []
        for member in archive.getmembers():
            if not member.isfile() or not member.name.startswith(prefixes):
                continue
            relative = Path(member.name).relative_to("mbedtls-4.1.1")
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("unsafe Mbed TLS archive path")
            member.name = relative.as_posix()
            members.append(member)
        archive.extractall(destination, members=members, filter="data")
    return destination


def mbedtls_sources(root: Path) -> tuple[Path, ...]:
    sources = [root / "library" / name for name in MBEDTLS_LIBRARY_NAMES]
    sources.extend(root / "tf-psa-crypto" / name
                   for name in MBEDTLS_SUPPORT_NAMES)
    sources.extend(sorted(
        path for path in (root / "tf-psa-crypto/core").glob("*.c")
        if path.name not in MBEDTLS_CORE_EXCLUDED))
    sources.extend(sorted(
        (root / "tf-psa-crypto/drivers/builtin/src").glob("*.c")))
    if any(not path.is_file() for path in sources):
        raise FileNotFoundError("pinned Mbed TLS source graph is incomplete")
    return tuple(sources)


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
    # Conventional freestanding compiler support, from the selected toolchain.
    # CSS uses signed 64-bit intermediates; do not couple it to TLS's private
    # arithmetic shim or provide incomplete compiler ABI stubs in the parser.
    builtins = artifacts.library_dir / "libclang_rt.builtins-i386.a"
    compiler_source = zig.parent / "lib/compiler_rt.zig"
    compiler_inputs = (zig, compiler_source, *compiler_source.with_suffix("").rglob("*.zig"), Path(__file__))
    if artifact_requires_rebuild(builtins, compiler_inputs, incremental):
        artifacts.library_dir.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="reist-builtins-") as temporary_name:
            temporary = Path(temporary_name)
            environment = os.environ.copy()
            environment["ZIG_GLOBAL_CACHE_DIR"] = str(artifacts.root / "builtins-cache")
            environment["ZIG_LOCAL_CACHE_DIR"] = str(temporary / "cache")
            obj = temporary / "builtins.o"
            subprocess.run([str(zig), "build-obj", str(compiler_source), "-target", "x86-freestanding-none",
                "-mcpu=i386", "-O", "ReleaseSmall", "-fno-stack-check", "-fno-stack-protector",
                "-fno-compiler-rt", "-femit-bin=" + str(obj)], env=environment, check=True, timeout=30)
            create_archive(zig,builtins,[obj],temporary,environment)
        license_file = artifacts.root / "usr/share/licenses/zig-compiler-rt/LICENSE"
        license_file.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(zig.parent / "LICENSE",license_file)
    libc_headers = tuple(LIBC_INCLUDE_ROOT.rglob("*.h"))
    for header in libc_headers:
        destination = artifacts.libc_include_dir / header.relative_to(LIBC_INCLUDE_ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_bytes() != header.read_bytes():
            shutil.copy2(header, destination)
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
    audio_headers = tuple(
        header for root, header in public_headers
        if root == AUDIO_INCLUDE_ROOT)
    storage_headers = tuple(
        header for root, header in public_headers
        if root == STORAGE_INCLUDE_ROOT)
    image_headers = tuple(
        header for root, header in public_headers
        if root == IMAGE_INCLUDE_ROOT)
    config_headers = tuple(
        header for root, header in public_headers
        if root == CONFIG_INCLUDE_ROOT)
    tls_headers = tuple(
        header for root, header in public_headers
        if root == TLS_INCLUDE_ROOT)
    all_sources = (
        STARTUP_SOURCE, *CORE_LIBRARY_SOURCES,
        *NETWORK_PARSER_SOURCES, *GUI_LIBRARY_SOURCES,
        *AUDIO_LIBRARY_SOURCES,
        *IMAGE_LIBRARY_SOURCES,
        *TLS_WRAPPER_SOURCES,
    )
    if (not core_headers or not gui_headers or not audio_headers or
            not storage_headers or
            not image_headers or not config_headers or not tls_headers or any(
            not source.is_file()
            for source in (*all_sources, *core_headers, *gui_headers,
                           *audio_headers, *storage_headers, *image_headers,
                           *config_headers, *tls_headers, MBEDTLS_ARCHIVE))):
        raise FileNotFoundError("REIST SDK sources are incomplete")

    for root, header in public_headers:
        destination = artifacts.include_dir / header.relative_to(root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if (not destination.is_file() or
                destination.read_bytes() != header.read_bytes()):
            shutil.copy2(header, destination)
    artifacts.library_dir.mkdir(parents=True, exist_ok=True)
    write_pkg_config(artifacts.library_dir)
    from build_html_engine import build as build_html_libraries
    build_html_libraries(artifacts, zig, incremental)
    common = ("prefix=${pcfiledir}/../..\nincludedir=${prefix}/include\n"
              "libdir=${prefix}/lib\n\n")
    write_if_changed(artifacts.library_dir / "pkgconfig/reist-c.pc", common +
                    "Name: reist-c\nDescription: Opt-in bounded C11 memory/byte subset\n"
                    "Version: 1.0.0\nCflags: -I${includedir}/reist/libc\n"
                    "Libs: -L${libdir} -lreistc -lreistos\n")
    write_if_changed(artifacts.library_dir / "pkgconfig/libwapcaplet.pc", common +
                    "Name: libwapcaplet\nDescription: NetSurf string interning for REIST\n"
                    "Version: 0.4.3\nRequires: reist-c\n"
                    "Cflags: -I${includedir}\nLibs: -L${libdir} -lwapcaplet\n")
    cpp_headers = tuple(p for p in CPP_INCLUDE_ROOT.rglob("*") if p.is_file())
    for header in cpp_headers:
        destination = artifacts.cpp_include_dir / header.relative_to(CPP_INCLUDE_ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_file() or destination.read_bytes() != header.read_bytes():
            shutil.copy2(header, destination)
    write_if_changed(artifacts.library_dir / "pkgconfig/reist-cpp.pc", common +
                    "Name: reist-cpp\nDescription: Opt-in freestanding C++20 profile 1\n"
                    "Version: 1.0.0\nRequires: reist-c\n"
                    "Cflags: -I${includedir}/reist/cpp " + " ".join(cpp_compile_flags()) + "\n"
                    "Libs: -L${libdir} -lreistcpp -lreistc -lreistos\n")
    cpp_stale = artifact_requires_rebuild(
        artifacts.cpp_library,
        (CPP_ROOT / "runtime.cpp", *cpp_headers, *libc_headers, *core_headers,
         Path(__file__), ROOT / "scripts/build_user_program.py"), incremental)

    startup_stale = artifact_requires_rebuild(
        artifacts.startup_object,
        (STARTUP_SOURCE, *core_headers), incremental)
    core_stale = artifact_requires_rebuild(
        artifacts.core_library,
        (*CORE_LIBRARY_SOURCES, *core_headers, *config_headers,
         *storage_headers), incremental)
    parser_stale = artifact_requires_rebuild(
        artifacts.network_parser_library,
        (*NETWORK_PARSER_SOURCES, *core_headers), incremental)
    gui_stale = artifact_requires_rebuild(
        artifacts.gui_library,
        (*GUI_LIBRARY_SOURCES, *core_headers, *gui_headers), incremental)
    audio_stale = artifact_requires_rebuild(
        artifacts.audio_library,
        (*AUDIO_LIBRARY_SOURCES, *core_headers, *audio_headers,
         *storage_headers), incremental)
    image_stale = artifact_requires_rebuild(
        artifacts.image_library,
        (*IMAGE_LIBRARY_SOURCES, *image_headers), incremental)
    tls_stale = artifact_requires_rebuild(
        artifacts.tls_library,
        (*TLS_WRAPPER_SOURCES, *tls_headers, MBEDTLS_ARCHIVE,
         TLS_LIBRARY_ROOT / "reist_tls_config.h"), incremental)
    libc_stale = artifact_requires_rebuild(
        artifacts.libc_library, (*LIBC_SOURCES, *libc_headers, *core_headers), incremental)
    wapcaplet_stale = artifact_requires_rebuild(
        artifacts.wapcaplet_library,
        (WAPCAPLET_ARCHIVE, Path(__file__).resolve(), *libc_headers), incremental)
    if not (startup_stale or core_stale or parser_stale or gui_stale or
            audio_stale or image_stale or tls_stale or libc_stale or wapcaplet_stale or cpp_stale):
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
        prefix = freestanding_compile_prefix(
            zig, [GUI_INCLUDE_ROOT, AUDIO_INCLUDE_ROOT, IMAGE_INCLUDE_ROOT,
                  CONFIG_INCLUDE_ROOT, STORAGE_INCLUDE_ROOT])

        if cpp_stale:
            cpp_object = temporary_path / "cpp-runtime.o"
            run([*freestanding_compile_prefix(zig, [LIBC_INCLUDE_ROOT, CPP_INCLUDE_ROOT]),
                 *cpp_compile_flags(), "-c", str(CPP_ROOT / "runtime.cpp"),
                 "-o", str(cpp_object)], environment)
            validate_cpp_object(cpp_object.read_bytes())
            create_archive(zig, artifacts.cpp_library, [cpp_object], temporary_path, environment)

        if libc_stale:
            objects = compile_objects(LIBC_SOURCES,
                freestanding_compile_prefix(zig, [LIBC_INCLUDE_ROOT]),
                temporary_path, "libc", environment)
            create_archive(zig, artifacts.libc_library, objects, temporary_path, environment)
        if wapcaplet_stale:
            vendor = extract_wapcaplet(temporary_path / "wapcaplet")
            public = artifacts.include_dir / "libwapcaplet/libwapcaplet.h"
            public.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(vendor / "include/libwapcaplet/libwapcaplet.h", public)
            license_path = artifacts.root / "usr/share/licenses/libwapcaplet/COPYING"
            license_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(vendor / "COPYING", license_path)
            objects = compile_objects((vendor / "src/libwapcaplet.c",),
                freestanding_compile_prefix(zig, [LIBC_INCLUDE_ROOT, vendor / "include"]),
                temporary_path, "wapcaplet", environment)
            create_archive(zig, artifacts.wapcaplet_library, objects, temporary_path, environment)

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
        if audio_stale:
            audio_objects = compile_objects(
                AUDIO_LIBRARY_SOURCES, prefix, temporary_path, "audio",
                environment)
            create_archive(
                zig, artifacts.audio_library, audio_objects,
                temporary_path, environment)
        if image_stale:
            image_objects = compile_objects(
                IMAGE_LIBRARY_SOURCES, prefix, temporary_path, "image",
                environment)
            create_archive(
                zig, artifacts.image_library, image_objects,
                temporary_path, environment)
        if tls_stale:
            vendor = extract_mbedtls(temporary_path / "mbedtls")
            tls_includes = [
                TLS_INCLUDE_ROOT, TLS_LIBRARY_ROOT,
                TLS_LIBRARY_ROOT / "compat", vendor,
                vendor / "include", vendor / "library",
                vendor / "tf-psa-crypto/include",
                vendor / "tf-psa-crypto/core",
                vendor / "tf-psa-crypto/drivers/builtin/include",
                vendor / "tf-psa-crypto/drivers/builtin/src",
                vendor / "tf-psa-crypto/extras",
                vendor / "tf-psa-crypto/utilities",
                vendor / "tf-psa-crypto/dispatch",
                vendor / "tf-psa-crypto/platform",
            ]
            tls_prefix = freestanding_compile_prefix(zig, tls_includes)
            tls_flags = [
                "-ffunction-sections", "-fdata-sections",
                '-DMBEDTLS_CONFIG_FILE="reist_tls_config.h"',
                '-DTF_PSA_CRYPTO_CONFIG_FILE="reist_tls_config.h"',
            ]
            tls_objects = compile_objects(
                (*TLS_WRAPPER_SOURCES, *mbedtls_sources(vendor)), tls_prefix,
                temporary_path, "tls", environment, tls_flags)
            create_archive(
                zig, artifacts.tls_library, tls_objects,
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
    print(f"REIST SDK audio library: {artifacts.audio_library}")
    print(f"REIST SDK image library: {artifacts.image_library}")
    print(f"REIST SDK TLS library: {artifacts.tls_library}")


if __name__ == "__main__":
    main()
