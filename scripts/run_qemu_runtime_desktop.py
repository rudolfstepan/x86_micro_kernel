"""Prove desktop.prg activates native graphics after a VGA text boot."""

from __future__ import annotations

import argparse
import binascii
import pathlib
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import zlib

from run_qemu_smoke import (
    QEMU_MUX_SWITCH,
    SHELL_PROMPT,
    monitor_key_commands,
    stop_process,
)


METRICS_VERSION = 1
RENDER_PROBE_STEPS = 8
METRIC_KEYS = {
    "version", "full_frames", "full_total_ms", "full_max_ms",
    "dirty_frames", "dirty_total_ms", "dirty_max_ms",
    "drag_frames", "drag_total_ms", "drag_max_ms",
    "resize_frames", "resize_total_ms", "resize_max_ms",
    "fallback_frames", "damage_regions", "damage_max",
    "clock_errors", "probe_errors",
}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def parse_render_metrics(text: str) -> tuple[dict[str, int], str]:
    match = re.search(r"DESKTOP_METRICS(?:\s+[a-z_]+=[0-9]+)+", text)
    if match is None:
        raise RuntimeError("desktop render metrics not observed")
    pairs = re.findall(r"([a-z_]+)=([0-9]+)", match.group(0))
    metrics = {name: int(value) for name, value in pairs}
    if len(metrics) != len(pairs) or set(metrics) != METRIC_KEYS:
        raise RuntimeError("desktop render metrics are missing or duplicated")
    if metrics["version"] != METRICS_VERSION:
        raise RuntimeError("desktop render metrics version mismatch")
    if (metrics["full_frames"] != 1 or
            metrics["drag_frames"] != RENDER_PROBE_STEPS or
            metrics["resize_frames"] != RENDER_PROBE_STEPS or
            metrics["dirty_frames"] != 2 * RENDER_PROBE_STEPS):
        raise RuntimeError("desktop render probe frame counts are invalid")
    if (metrics["fallback_frames"] != 0 or
            metrics["clock_errors"] != 0 or
            metrics["probe_errors"] != 0):
        raise RuntimeError("desktop render probe reported an error")
    if not 1 <= metrics["damage_max"] <= 8:
        raise RuntimeError("desktop render damage bound is invalid")
    total_frames = metrics["full_frames"] + metrics["dirty_frames"]
    if not total_frames <= metrics["damage_regions"] <= total_frames * 8:
        raise RuntimeError("desktop render damage count is invalid")
    for prefix in ("full", "dirty", "drag", "resize"):
        if metrics[f"{prefix}_total_ms"] < metrics[f"{prefix}_max_ms"]:
            raise RuntimeError(f"desktop {prefix} timing is inconsistent")
    normalized = "DESKTOP_METRICS " + " ".join(
        f"{name}={value}" for name, value in pairs
    )
    return metrics, normalized


def reader(stream, output: queue.Queue[str], finished: threading.Event) -> None:
    try:
        while True:
            value = stream.read(1)
            if not value:
                return
            output.put(value)
    finally:
        finished.set()


def drain(output: queue.Queue[str], transcript: list[str]) -> None:
    while True:
        try:
            transcript.append(output.get_nowait())
        except queue.Empty:
            return


def desktop_monitor_key_commands(command: str) -> list[str]:
    keys: list[str] = []
    parts = command.split("-")
    for index, part in enumerate(parts):
        keys.extend(monitor_key_commands(part)[:-1])
        if index + 1 < len(parts):
            keys.append("sendkey minus\n")
    keys.append("sendkey ret\n")
    return keys


def send_command(process: subprocess.Popen[str], command: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(0.05)
    for key in desktop_monitor_key_commands(command):
        process.stdin.write(key)
        process.stdin.flush()
        time.sleep(0.02)
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()


def send_key(process: subprocess.Popen[str], key: str) -> None:
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(0.05)
    process.stdin.write(f"sendkey {key}\n")
    process.stdin.flush()
    time.sleep(0.05)
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes] | None:
    """Read the bounded P6 format emitted by QEMU's screendump command."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    header = re.match(
        rb"P6[ \t\r\n]+([0-9]+)[ \t\r\n]+([0-9]+)"
        rb"[ \t\r\n]+255[ \t\r\n]",
        data,
    )
    if header is None:
        return None
    width = int(header.group(1))
    height = int(header.group(2))
    offset = header.end()
    expected = width * height * 3
    if width <= 0 or height <= 0 or len(data) != offset + expected:
        return None
    return width, height, data[offset:]


def screenshot_has_menu_text(path: pathlib.Path) -> bool:
    """Require dark glyph pixels inside the otherwise gray desktop menu bar."""
    ppm = read_ppm(path)
    if ppm is None:
        return False
    width, height, pixels = ppm
    if width < 460 or height < 30:
        return False

    dark_pixels = 0
    for y in range(8, 24):
        row = y * width * 3
        for x in range(10, 450):
            pixel = row + x * 3
            if (pixels[pixel] < 96 and pixels[pixel + 1] < 96 and
                    pixels[pixel + 2] < 96):
                dark_pixels += 1
    return dark_pixels >= 64


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", checksum))


def convert_screenshot_if_png(path: pathlib.Path) -> None:
    """Convert QEMU P6 output only when the requested suffix is ``.png``."""
    if path.suffix.lower() != ".png":
        return
    ppm = read_ppm(path)
    if ppm is None:
        raise RuntimeError("QEMU screenshot is not a valid P6 image")
    width, height, pixels = ppm
    scanlines = b"".join(
        b"\0" + pixels[row * width * 3:(row + 1) * width * 3]
        for row in range(height)
    )
    png = bytearray(PNG_SIGNATURE)
    png.extend(png_chunk(
        b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    ))
    png.extend(png_chunk(b"IDAT", zlib.compress(scanlines, level=9)))
    png.extend(png_chunk(b"IEND", b""))
    path.write_bytes(png)


def require_screenshot_menu_text(path: pathlib.Path,
                                 deadline: float) -> None:
    while time.monotonic() < deadline:
        if screenshot_has_menu_text(path):
            return
        time.sleep(0.02)
    raise RuntimeError("desktop screenshot contains no menu text")


def capture_screenshot(process: subprocess.Popen[str],
                       screenshot: pathlib.Path,
                       deadline: float) -> None:
    """Capture, validate and optionally convert one bounded guest frame."""
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(0.05)
    process.stdin.write(f"screendump {screenshot}\n")
    process.stdin.flush()
    time.sleep(0.1)
    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    require_screenshot_menu_text(
        screenshot, min(deadline, time.monotonic() + 1.0)
    )
    convert_screenshot_if_png(screenshot)


def run(qemu: pathlib.Path, image: pathlib.Path, screenshot: pathlib.Path,
        timeout: float, expect_failure: bool, render_probe: bool,
        surface_probe: bool, notepad_probe: bool,
        metrics_log: pathlib.Path | None) -> int:
    command = [
        str(qemu), "-accel", "tcg", "-machine", "pc", "-nodefaults",
        "-device", "VGA,vgamem_mb=1" if expect_failure else "VGA",
        "-m", "512M", "-display", "none",
        "-monitor", "none", "-serial", "mon:stdio", "-no-reboot",
        "-snapshot", "-drive", f"file={image},format=raw,if=ide,index=0",
    ]
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace", bufsize=0)
    assert process.stdout is not None
    output: queue.Queue[str] = queue.Queue()
    finished = threading.Event()
    thread = threading.Thread(target=reader,
                              args=(process.stdout, output, finished), daemon=True)
    thread.start()
    transcript: list[str] = []
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            drain(output, transcript)
            text = "".join(transcript)
            if SHELL_PROMPT in text:
                command_name = "desktop.prg --render-probe" if render_probe \
                    else ("desktop.prg --surface-probe" if surface_probe
                          else ("desktop.prg --notepad-probe"
                                if notepad_probe else "desktop.prg"))
                send_command(process, command_name)
                break
            time.sleep(0.02)
        else:
            raise RuntimeError("VGA shell prompt not observed")
        while time.monotonic() < deadline:
            drain(output, transcript)
            text = "".join(transcript)
            if "DESKTOP_OK" in text:
                if expect_failure:
                    print("runtime-desktop: FAIL: unsupported mode was accepted",
                          file=sys.stderr)
                    return 1
                if render_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if ("DESKTOP_METRICS" in probe_text and
                                "DESKTOP_EXIT_OK" in probe_text):
                            exit_offset = probe_text.index("DESKTOP_EXIT_OK")
                            if SHELL_PROMPT in probe_text[exit_offset:]:
                                metrics, metric_line = parse_render_metrics(
                                    probe_text
                                )
                                if metrics_log is not None:
                                    metrics_log.parent.mkdir(
                                        parents=True, exist_ok=True
                                    )
                                    metrics_log.write_text(
                                        metric_line + "\n", encoding="utf-8"
                                    )
                                print(
                                    "runtime-desktop-metrics: PASS "
                                    f"full_max_ms={metrics['full_max_ms']} "
                                    f"drag_max_ms={metrics['drag_max_ms']} "
                                    f"resize_max_ms={metrics['resize_max_ms']}"
                                )
                                return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "desktop render probe did not restore the VGA shell"
                    )
                if surface_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "SURFACE_DEMO_FAIL" in probe_text:
                            marker = re.findall(
                                r"SURFACE_DEMO_FAIL[^\r\n]*", probe_text
                            )[-1]
                            raise RuntimeError(
                                f"Surface client failed: {marker}"
                            )
                        if "DESKTOP_SURFACE_FAIL" in probe_text:
                            marker = re.findall(
                                r"DESKTOP_SURFACE_FAIL[^\r\n]*", probe_text
                            )[-1]
                            raise RuntimeError(
                                f"Surface probe failed: {marker}"
                            )
                        if "notepad: Surface" in probe_text:
                            marker = re.findall(
                                r"notepad: Surface[^\r\n]*", probe_text
                            )[-1]
                            raise RuntimeError(
                                f"Editor Surface failed: {marker}"
                            )
                        if ("DESKTOP_SURFACE_OK" in probe_text and
                            "NOTEPAD_SURFACE_READY" in probe_text and
                            "IMAGEVIEWER_SURFACE_READY" in probe_text and
                                "NOTEPAD_SURFACE_DOCUMENT_READY" in
                                probe_text and
                                "NOTEPAD_SURFACE_MENU_READY" in probe_text and
                                "NOTEPAD_SURFACE_FILE_DIALOG_READY" in
                                probe_text and
                                "NOTEPAD_SURFACE_HOVER_READY" in probe_text):
                            time.sleep(0.2)
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-surface: PASS")
                            return 0
                        time.sleep(0.02)
                    tail = "".join(transcript)[-1600:].replace("\r", "")
                    raise RuntimeError(
                        "Surface client did not publish a visible window; "
                        f"guest tail:\n{tail}"
                    )
                if notepad_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_NOTEPAD_FAIL" in probe_text:
                            raise RuntimeError("Notepad probe launch failed")
                        if "notepad: Surface" in probe_text:
                            marker = re.findall(
                                r"notepad: Surface[^\r\n]*", probe_text
                            )[-1]
                            raise RuntimeError(
                                f"Editor Surface failed: {marker}"
                            )
                        if ("NOTEPAD_SURFACE_READY" in probe_text and
                                "NOTEPAD_SURFACE_DOCUMENT_READY" in
                                probe_text):
                            time.sleep(0.2)
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-notepad: PASS")
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "Notepad did not publish a visible document window"
                    )
                # The marker is emitted immediately before the single
                # backbuffer render so it is overwritten by the desktop.
                # Give the guest a bounded interval to finish that frame
                # before capturing it or injecting Escape.
                time.sleep(0.2)
                capture_screenshot(process, screenshot, deadline)
                send_key(process, "esc")
                while time.monotonic() < deadline:
                    drain(output, transcript)
                    exited = "DESKTOP_EXIT_OK" in "".join(transcript)
                    if exited:
                        exit_offset = "".join(transcript).index(
                            "DESKTOP_EXIT_OK")
                        if SHELL_PROMPT in "".join(transcript)[exit_offset:]:
                            print("runtime-desktop: PASS")
                            return 0
                    time.sleep(0.02)
                raise RuntimeError("desktop did not restore the VGA shell")
            if "DISPLAY_CONTROL: native graphics unavailable" in text:
                raise RuntimeError("native runtime graphics activation failed")
            time.sleep(0.02)
        drain(output, transcript)
        tail = "".join(transcript)[-1200:].replace("\r", "")
        raise RuntimeError(f"DESKTOP_OK marker not observed; guest tail:\n{tail}")
    finally:
        stop_process(process)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=pathlib.Path, required=True)
    parser.add_argument("--image", type=pathlib.Path, required=True)
    parser.add_argument("--screenshot", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--expect-failure", action="store_true")
    parser.add_argument("--render-probe", action="store_true")
    parser.add_argument("--surface-probe", action="store_true")
    parser.add_argument("--notepad-probe", action="store_true")
    parser.add_argument("--metrics-log", type=pathlib.Path)
    args = parser.parse_args()
    if sum((args.expect_failure, args.render_probe, args.surface_probe,
            args.notepad_probe)) > 1:
        parser.error("desktop probe modes are mutually exclusive")
    if args.metrics_log is not None and not args.render_probe:
        parser.error("--metrics-log requires --render-probe")
    try:
        return run(args.qemu, args.image, args.screenshot, args.timeout,
                   args.expect_failure, args.render_probe,
                   args.surface_probe, args.notepad_probe, args.metrics_log)
    except (OSError, RuntimeError) as error:
        print(f"runtime-desktop: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
