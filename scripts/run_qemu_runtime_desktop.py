"""Prove desktop.prg activates native graphics after a VGA text boot."""

from __future__ import annotations

import argparse
import array
import binascii
import pathlib
import queue
import re
import socket
import struct
import subprocess
import sys
import threading
import time
import wave
import zlib

from run_qemu_smoke import (
    QEMU_MUX_SWITCH,
    SHELL_PROMPT,
    TCP_TEST_MAC,
    TCP_TEST_TARGET,
    monitor_key_commands,
    open_injection_listener,
    qemu_command,
    qemu_monitor_command,
    serve_dns_a_query,
    stop_process,
)
from run_qemu_pci_audio import finalize_qemu_wave


METRICS_VERSION = 1
RENDER_PROBE_STEPS = 8
MAXIMUM_HOVER_FRAME_MS = 17
MAXIMUM_POINTER_GAP_MS = 34
SHELL_HELP_MARKER = "Built-ins: cd path pwd history help exit"
QEMU_CREATION_FLAGS = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)
QEMU_SENDKEY_INTERVAL_SECONDS = 0.12
METRIC_KEYS = {
    "version", "full_frames", "full_total_ms", "full_max_ms",
    "dirty_frames", "dirty_total_ms", "dirty_max_ms",
    "drag_frames", "drag_total_ms", "drag_max_ms",
    "resize_frames", "resize_total_ms", "resize_max_ms",
    "fallback_frames", "damage_regions", "damage_max",
    "clock_errors", "probe_errors",
}
HOVER_METRIC_KEYS = {
    "version", "items", "frames", "full_frames", "total_ms", "max_ms",
    "damage_max", "mouse_reports", "mouse_batch_max_ms",
    "mouse_batch_max_reports", "pointer_frames",
    "pointer_max_gap_ms", "pointer_latency_max_ms", "pointer_call_max_ms",
    "pointer_failures", "order_errors", "clock_errors",
}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
NOTEPAD_REFERENCE_HOST = "intracom.at"
NOTEPAD_REFERENCE_QUESTION = b"\x08intracom\x02at\x00\x00\x01\x00\x01"


def resolve_notepad_reference_ipv4(
        deadline: float) -> tuple[bytes | None, str | None]:
    """Resolve the exact Notepad reference host under a fixed host deadline."""
    results: queue.Queue[tuple[bytes | None, str | None]] = queue.Queue(1)

    def resolve() -> None:
        try:
            addresses = socket.getaddrinfo(
                NOTEPAD_REFERENCE_HOST, 443,
                socket.AF_INET, socket.SOCK_STREAM)
            if not addresses:
                results.put((None, "intracom.at returned no IPv4 address"))
                return
            results.put((socket.inet_aton(addresses[0][4][0]), None))
        except OSError as error:
            results.put((None, f"intracom.at resolution failed: {error}"))

    threading.Thread(target=resolve, daemon=True).start()
    try:
        return results.get(timeout=min(
            3.0, max(0.01, deadline - time.monotonic())))
    except queue.Empty:
        return None, "intracom.at resolution deadline expired"


def validate_system_sound_wave(path: pathlib.Path) -> tuple[bool, str]:
    """Require one bounded non-silent stereo S16 system-sound interval."""
    if not path.is_file():
        return False, "QEMU did not create the audio capture"
    try:
        with wave.open(str(path), "rb") as capture:
            channels = capture.getnchannels()
            width = capture.getsampwidth()
            rate = capture.getframerate()
            frames = capture.getnframes()
            payload = capture.readframes(frames)
    except (OSError, EOFError, wave.Error) as error:
        return False, f"invalid WAV capture: {error}"
    if channels != 2 or width != 2 or rate != 48000 or frames == 0:
        return False, (f"unexpected WAV format channels={channels} "
                       f"width={width} rate={rate} frames={frames}")
    samples = array.array("h")
    samples.frombytes(payload)
    if sys.byteorder != "little":
        samples.byteswap()
    left = samples[0::channels]
    right = samples[1::channels]
    if left != right:
        return False, "system-sound capture is not duplicated mono stereo"
    first = next((index for index, value in enumerate(left) if value != 0), -1)
    last = next((len(left) - 1 - index for index, value in
                 enumerate(reversed(left)) if value != 0), -1)
    if first < 0 or last <= first:
        return False, "system-sound capture contains silence only"
    active = left[first:last + 1]
    if len(active) < rate // 20:
        return False, f"system-sound interval too short: {len(active)} frames"
    if len(active) > rate // 2:
        return False, ("system-sound playback repeated instead of stopping: "
                       f"{len(active)} active frames")
    peak = max(abs(value) for value in active)
    if peak < 512:
        return False, f"system-sound level is too low: peak={peak}"
    return True, (f"stereo S16 system sound frames={frames} "
                  f"active={len(active)} peak={peak}")


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
        raise RuntimeError(
            "desktop render probe reported an error: " + match.group(0)
        )
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


def parse_hover_metrics(text: str) -> tuple[dict[str, int], str]:
    match = re.search(
        r"DESKTOP_HOVER_METRICS(?:\s+[a-z_]+=[0-9]+)+", text
    )
    if match is None:
        raise RuntimeError("desktop hover metrics not observed")
    pairs = re.findall(r"([a-z_]+)=([0-9]+)", match.group(0))
    metrics = {name: int(value) for name, value in pairs}
    if len(metrics) != len(pairs) or set(metrics) != HOVER_METRIC_KEYS:
        raise RuntimeError("desktop hover metrics are missing or duplicated")
    if (metrics["version"] != 1 or metrics["items"] != 6 or
            metrics["frames"] != 6):
        raise RuntimeError("desktop hover coverage is invalid")
    if metrics["full_frames"] != 0 or not 1 <= metrics["damage_max"] <= 2:
        raise RuntimeError("desktop hover used excessive damage")
    if metrics["max_ms"] > MAXIMUM_HOVER_FRAME_MS:
        raise RuntimeError(
            "desktop hover frame missed the frozen maximum: "
            f"{metrics['max_ms']}/{MAXIMUM_HOVER_FRAME_MS} ms"
        )
    if (metrics["mouse_batch_max_reports"] < 1 or
            metrics["mouse_batch_max_reports"] > 4 or
            metrics["mouse_batch_max_ms"] > MAXIMUM_POINTER_GAP_MS):
        raise RuntimeError(
            "desktop mouse batch missed the frozen bound: "
            f"reports={metrics['mouse_batch_max_reports']}/4 "
            f"elapsed={metrics['mouse_batch_max_ms']}/"
            f"{MAXIMUM_POINTER_GAP_MS} ms"
        )
    if metrics["pointer_max_gap_ms"] > MAXIMUM_POINTER_GAP_MS:
        raise RuntimeError(
            "desktop pointer cadence missed the frozen maximum: "
            f"{metrics['pointer_max_gap_ms']}/{MAXIMUM_POINTER_GAP_MS} ms"
        )
    if (metrics["pointer_latency_max_ms"] > MAXIMUM_POINTER_GAP_MS or
            metrics["pointer_call_max_ms"] > MAXIMUM_POINTER_GAP_MS):
        raise RuntimeError(
            "desktop pointer service missed the frozen maximum: "
            f"latency={metrics['pointer_latency_max_ms']} "
            f"call={metrics['pointer_call_max_ms']}/"
            f"{MAXIMUM_POINTER_GAP_MS} ms"
        )
    if (metrics["mouse_reports"] < 6 or metrics["pointer_frames"] < 2 or
            metrics["pointer_failures"] != 0 or
            metrics["order_errors"] != 0 or metrics["clock_errors"] != 0):
        raise RuntimeError("desktop hover input proof is invalid")
    normalized = "DESKTOP_HOVER_METRICS " + " ".join(
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
    time.sleep(0.15)
    for key in desktop_monitor_key_commands(command):
        process.stdin.write(key)
        process.stdin.flush()
        time.sleep(QEMU_SENDKEY_INTERVAL_SECONDS)
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


def send_hover_trajectory(process: subprocess.Popen[str]) -> None:
    """Send one bounded center-to-Start path and seven real USB hot states."""
    if process.stdin is None:
        raise RuntimeError("QEMU monitor input unavailable")

    def monitor(command: str, delay: float) -> None:
        if "\n" in command or "\r" in command:
            raise RuntimeError("invalid QEMU hover command")
        assert process.stdin is not None
        process.stdin.write(command + "\n")
        process.stdin.flush()
        time.sleep(delay)

    process.stdin.write(QEMU_MUX_SWITCH)
    process.stdin.flush()
    time.sleep(0.05)
    try:
        previous_x = 0
        previous_y = 0
        for step in range(1, 33):
            target_x = -472 * step // 32
            target_y = 369 * step // 32
            monitor(
                f"mouse_move {target_x - previous_x} {target_y - previous_y}",
                0.016,
            )
            previous_x = target_x
            previous_y = target_y
        monitor("mouse_button 1", 0.024)
        monitor("mouse_button 0", 0.070)
        monitor("mouse_move 60 -26", 0.070)
        for _ in range(6):
            monitor("mouse_move 0 -24", 0.070)
    finally:
        process.stdin.write(QEMU_MUX_SWITCH)
        process.stdin.flush()


def send_desktop_exit_click(process: subprocess.Popen[str]) -> None:
    """Activate the production Start-menu exit item with real USB edges."""
    for command, delay in (
            ("mouse_move -472 369", 0.10),
            ("mouse_button 1", 0.08), ("mouse_button 0", 0.10),
            ("mouse_move 60 -26", 0.10),
            ("mouse_button 1", 0.08), ("mouse_button 0", 0.10)):
        qemu_monitor_command(process, command)
        time.sleep(delay)


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
    """Require dark glyph pixels inside the classic bottom taskbar."""
    ppm = read_ppm(path)
    if ppm is None:
        return False
    width, height, pixels = ppm
    if width < 320 or height < 30:
        return False

    dark_pixels = 0
    for y in range(height - 24, height - 6):
        row = y * width * 3
        for x in range(6, width - 6):
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


def wait_for_desktop_pattern(
        output: queue.Queue[str], transcript: list[str], pattern: str,
        deadline: float, after: int = 0) -> tuple[re.Match[str], str]:
    compiled = re.compile(pattern)
    while time.monotonic() < deadline:
        drain(output, transcript)
        text = "".join(transcript)
        for failure in (
                "REIST_GUI COMPOSITOR_RESTARTED",
                "REIST_GUI COMPOSITOR_DEGRADED",
                "DESKTOP_SHORTCUT_PROBE_FAIL"):
            failure_offset = text.find(failure, after)
            if failure_offset >= 0:
                raise RuntimeError(
                    f"desktop shortcut probe failed: {failure}; "
                    f"guest tail:\n{text[-8000:].replace(chr(13), '')}"
                )
        match = compiled.search(text, after)
        if match is not None:
            return match, text
        time.sleep(0.02)
    drain(output, transcript)
    text = "".join(transcript)
    raise RuntimeError(
        f"desktop shortcut marker not observed: {pattern}; "
        f"guest tail:\n{text[-8000:].replace(chr(13), '')}"
    )


def shortcut_probe_point(
        output: queue.Queue[str], transcript: list[str], marker: str,
        kind: str, deadline: float, after: int = 0) -> tuple[int, int]:
    match, _ = wait_for_desktop_pattern(
        output, transcript,
        rf"DESKTOP_SHORTCUT_PROBE_{marker} kind={kind} "
        r"x=([0-9]+) y=([0-9]+)",
        deadline, after,
    )
    return int(match.group(1)), int(match.group(2))


def shortcut_probe_display_size(
        screenshot: pathlib.Path, transcript: list[str]) -> tuple[int, int]:
    ppm = read_ppm(screenshot)
    if ppm is not None:
        return ppm[0], ppm[1]
    match = re.search(
        r"Framebuffer initialized: ([0-9]+)x([0-9]+)x[0-9]+",
        "".join(transcript),
    )
    if match is None:
        raise RuntimeError("desktop shortcut probe display size is unknown")
    return int(match.group(1)), int(match.group(2))


def shortcut_probe_move_mouse(
        process: subprocess.Popen[str], pointer: list[int],
        target_x: int, target_y: int) -> None:
    while pointer[0] != target_x or pointer[1] != target_y:
        delta_x = target_x - pointer[0]
        delta_y = target_y - pointer[1]
        step_x = max(-120, min(120, delta_x))
        step_y = max(-120, min(120, delta_y))
        qemu_monitor_command(process, f"mouse_move {step_x} {step_y}")
        pointer[0] += step_x
        pointer[1] += step_y
        time.sleep(0.04)


def shortcut_probe_click(
        process: subprocess.Popen[str], button: int,
        count: int = 1) -> None:
    for _ in range(count):
        qemu_monitor_command(process, f"mouse_button {button}")
        time.sleep(0.08)
        qemu_monitor_command(process, "mouse_button 0")
        time.sleep(0.12)


def shortcut_probe_drag(
        process: subprocess.Popen[str], pointer: list[int],
        source: tuple[int, int], destination: tuple[int, int]) -> None:
    shortcut_probe_move_mouse(process, pointer, source[0], source[1])
    qemu_monitor_command(process, "mouse_button 1")
    time.sleep(0.12)
    shortcut_probe_move_mouse(
        process, pointer, destination[0], destination[1])
    time.sleep(0.12)
    qemu_monitor_command(process, "mouse_button 0")
    time.sleep(0.25)


def shortcut_probe_context_action(
        process: subprocess.Popen[str], output: queue.Queue[str],
        transcript: list[str], pointer: list[int], target: tuple[int, int],
        action: str, deadline: float) -> int:
    shortcut_probe_move_mouse(process, pointer, target[0], target[1])
    event_offset = len("".join(transcript))
    shortcut_probe_click(process, 2)
    menu, text = wait_for_desktop_pattern(
        output, transcript,
        rf"DESKTOP_SHORTCUT_PROBE_MENU action={action} "
        r"x=([0-9]+) y=([0-9]+)",
        deadline, event_offset,
    )
    shortcut_probe_move_mouse(
        process, pointer, int(menu.group(1)), int(menu.group(2)))
    action_offset = len(text)
    shortcut_probe_click(process, 1)
    return action_offset


def run_shortcut_mouse_probe(
        process: subprocess.Popen[str], output: queue.Queue[str],
        transcript: list[str], screenshot: pathlib.Path,
        initial_deadline: float) -> int:
    deadline = max(initial_deadline, time.monotonic() + 120.0)
    _, initial_text = wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_SHORTCUTS_READY count=0", deadline)
    initial_offset = initial_text.rfind("DESKTOP_OK")
    file_target = shortcut_probe_point(
        output, transcript, "TARGET", "file", deadline, initial_offset)
    desktop_drop = shortcut_probe_point(
        output, transcript, "DROP", "desktop", deadline, initial_offset)
    source_drop = shortcut_probe_point(
        output, transcript, "DROP", "source", deadline, initial_offset)
    capture_screenshot(process, screenshot, deadline)
    width, height = shortcut_probe_display_size(screenshot, transcript)
    pointer = [width // 2, height // 2]

    outcome_offset = shortcut_probe_context_action(
        process, output, transcript, pointer, file_target,
        "create", deadline)
    created, created_text = wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_SHORTCUT_CREATED kind=file path=/htdocs/readme\.txt "
        r"shortcut=(/htdocs/[A-Za-z0-9_-]{1,8}\.LNK)",
        deadline, outcome_offset,
    )
    shortcut_path = created.group(1)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_SHORTCUT_SIBLING_OK path=" +
        re.escape(shortcut_path), deadline, outcome_offset)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_SHORTCUT_DESKTOP_UNCHANGED",
        deadline, outcome_offset)
    restart_offset = created.start()
    wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_SHORTCUT_STORAGE_RESTART_REQUESTED",
        deadline, restart_offset)
    wait_for_desktop_pattern(
        output, transcript,
        r"COMPONENT RESTART_OK component=5 generation=[0-9]+",
        deadline, restart_offset)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_SHORTCUT_STORAGE_RELOAD_OK count=0",
        deadline, restart_offset)
    sibling = shortcut_probe_point(
        output, transcript, "SIBLING", "shortcut", deadline,
        restart_offset)
    desktop_drop = shortcut_probe_point(
        output, transcript, "DROP", "desktop", deadline, restart_offset)
    move_offset = len("".join(transcript))
    shortcut_probe_drag(process, pointer, sibling, desktop_drop)
    wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_FILE_MOVE_OK source=" + re.escape(shortcut_path) +
        r" destination=/desktop/[A-Za-z0-9_-]{1,8}\.LNK",
        deadline, move_offset)
    shortcut_icon = shortcut_probe_point(
        output, transcript, "ICON", "shortcut", deadline, move_offset)
    capture_screenshot(process, screenshot, deadline)

    shortcut_probe_move_mouse(
        process, pointer, shortcut_icon[0], shortcut_icon[1])
    activation_offset = len("".join(transcript))
    shortcut_probe_click(process, 1, 2)
    wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_SHORTCUT_ACTIVATED kind=file "
        r"path=/htdocs/readme\.txt",
        deadline, activation_offset,
    )
    wait_for_desktop_pattern(
        output, transcript, r"NOTEPAD_SURFACE_READY",
        deadline, activation_offset,
    )
    wait_for_desktop_pattern(
        output, transcript, r"NOTEPAD_SURFACE_DOCUMENT_READY",
        deadline, activation_offset,
    )
    client_close = shortcut_probe_point(
        output, transcript, "CLOSE", "client", deadline,
        activation_offset)
    close_offset = len("".join(transcript))
    shortcut_probe_move_mouse(
        process, pointer, client_close[0], client_close[1])
    shortcut_probe_click(process, 1)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_SHORTCUT_CLIENT_CLOSED",
        deadline, close_offset,
    )

    source_drop = shortcut_probe_point(
        output, transcript, "DROP", "source", deadline, move_offset)
    move_offset = len("".join(transcript))
    shortcut_probe_drag(process, pointer, shortcut_icon, source_drop)
    wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_FILE_MOVE_OK source=/desktop/"
        r"[A-Za-z0-9_-]{1,8}\.LNK destination=" +
        re.escape(shortcut_path), deadline, move_offset)

    file_target = shortcut_probe_point(
        output, transcript, "TARGET", "file", deadline, move_offset)
    desktop_drop = shortcut_probe_point(
        output, transcript, "DROP", "desktop", deadline, move_offset)
    move_offset = len("".join(transcript))
    shortcut_probe_drag(process, pointer, file_target, desktop_drop)
    wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_FILE_MOVE_OK source=/htdocs/readme\.txt "
        r"destination=/desktop/readme\.txt",
        deadline, move_offset)
    file_icon = shortcut_probe_point(
        output, transcript, "ICON", "file", deadline, move_offset)
    source_drop = shortcut_probe_point(
        output, transcript, "DROP", "source", deadline, move_offset)
    move_offset = len("".join(transcript))
    shortcut_probe_drag(process, pointer, file_icon, source_drop)
    _, final_text = wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_FILE_MOVE_OK "
        r"source=(?i:/desktop/readme\.txt) "
        r"destination=(?i:/htdocs/readme\.txt)",
        deadline, move_offset)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_DIRECTORY_RELOAD count=0",
        deadline, move_offset)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_MOUSE_OK", deadline, initial_offset)
    if "DESKTOP_EXIT_OK" in final_text[initial_offset:]:
        raise RuntimeError(
            "desktop exited during the live storage-restart shortcut flow"
        )
    capture_screenshot(process, screenshot, deadline)
    print("runtime-desktop-shortcuts: PASS")
    return 0


def icon_layout_probe_point(
        output: queue.Queue[str], transcript: list[str], marker: str,
        index: int, deadline: float, after: int = 0) -> tuple[int, int]:
    match, _ = wait_for_desktop_pattern(
        output, transcript,
        rf"DESKTOP_ICON_LAYOUT_{marker} index={index} "
        r"(?:kind=[a-z]+ )?x=([0-9]+) y=([0-9]+)",
        deadline, after,
    )
    return int(match.group(1)), int(match.group(2))


def run_icon_layout_mouse_probe(
        process: subprocess.Popen[str], output: queue.Queue[str],
        transcript: list[str], screenshot: pathlib.Path,
        initial_deadline: float) -> int:
    deadline = max(initial_deadline, time.monotonic() + 90.0)
    ready, initial_text = wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_ICON_LAYOUT_READY width=([0-9]+) height=([0-9]+) "
        r"count=([0-9]+) columns=([0-9]+) rows=([0-9]+)",
        deadline,
    )
    if int(ready.group(3)) < 4:
        raise RuntimeError("desktop layout probe did not create a shortcut")
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_ICON_LAYOUT_RESIZE_OK "
        r"width=[0-9]+ height=[0-9]+", deadline)
    initial_offset = initial_text.rfind("DESKTOP_OK")
    computer = icon_layout_probe_point(
        output, transcript, "ICON", 0, deadline, initial_offset)
    computer_target = icon_layout_probe_point(
        output, transcript, "DROP_TARGET", 0, deadline, initial_offset)
    capture_screenshot(process, screenshot, deadline)
    width, height = shortcut_probe_display_size(screenshot, transcript)
    pointer = [width // 2, height // 2]

    move_offset = len("".join(transcript))
    shortcut_probe_drag(process, pointer, computer, computer_target)
    _, moved_text = wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_ICON_LAYOUT_DROP_OK index=0 column=[0-9]+ row=[0-9]+",
        deadline, move_offset,
    )
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_ICON_LAYOUT_RELOAD_OK",
        deadline, move_offset)
    geometry_offset = moved_text.find("DESKTOP_ICON_LAYOUT_READY", move_offset)
    if geometry_offset < 0:
        geometry_offset = move_offset
    shortcut_match, _ = wait_for_desktop_pattern(
        output, transcript,
        r"DESKTOP_ICON_LAYOUT_ICON index=([3-9][0-9]*) kind=shortcut "
        r"x=([0-9]+) y=([0-9]+)", deadline, geometry_offset)
    shortcut_index = int(shortcut_match.group(1))
    shortcut = (int(shortcut_match.group(2)), int(shortcut_match.group(3)))
    shortcut_target = icon_layout_probe_point(
        output, transcript, "DROP_TARGET", shortcut_index,
        deadline, geometry_offset)

    shortcut_move_offset = len("".join(transcript))
    shortcut_probe_drag(process, pointer, shortcut, shortcut_target)
    _, second_text = wait_for_desktop_pattern(
        output, transcript,
        rf"DESKTOP_ICON_LAYOUT_DROP_OK index={shortcut_index} "
        r"column=[0-9]+ row=[0-9]+",
        deadline, shortcut_move_offset,
    )
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_ICON_LAYOUT_RELOAD_OK",
        deadline, shortcut_move_offset)
    second_geometry = second_text.find(
        "DESKTOP_ICON_LAYOUT_READY", shortcut_move_offset)
    if second_geometry < 0:
        second_geometry = shortcut_move_offset
    shortcut = icon_layout_probe_point(
        output, transcript, "ICON", shortcut_index,
        deadline, second_geometry)
    shortcut_probe_move_mouse(process, pointer, shortcut[0], shortcut[1])
    activation_offset = len("".join(transcript))
    shortcut_probe_click(process, 1, 2)
    _, final_text = wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_ICON_LAYOUT_ACTIVATED",
        deadline, activation_offset)
    wait_for_desktop_pattern(
        output, transcript, r"NOTEPAD_SURFACE_DOCUMENT_READY",
        deadline, activation_offset)
    wait_for_desktop_pattern(
        output, transcript, r"DESKTOP_MOUSE_OK", deadline, initial_offset)
    for failure in (
            "DESKTOP_ICON_LAYOUT_PROBE_FAIL",
            "REIST_GUI COMPOSITOR_RESTARTED",
            "REIST_GUI COMPOSITOR_DEGRADED",
            "DESKTOP_EXIT_OK"):
        if failure in final_text[initial_offset:]:
            raise RuntimeError(f"desktop layout probe observed {failure}")
    capture_screenshot(process, screenshot, deadline)
    print("runtime-desktop-icon-layout: PASS")
    return 0


def run_desktop_relaunch_probe(
        process: subprocess.Popen[str], output: queue.Queue[str],
        transcript: list[str]) -> int:
    first_exit_offset = len("".join(transcript))
    send_desktop_exit_click(process)
    deadline = time.monotonic() + 45.0
    while time.monotonic() < deadline:
        drain(output, transcript)
        text = "".join(transcript)
        tail = text[first_exit_offset:]
        if ("REIST_GUI COMPOSITOR_RESTARTED" in tail or
                "REIST_GUI COMPOSITOR_DEGRADED" in tail):
            raise RuntimeError("clean desktop exit entered recovery")
        if ("DESKTOP_EXIT_OK" in tail and
                "REIST_GUI COMPOSITOR_STOPPED epoch=" in tail):
            break
        time.sleep(0.02)
    else:
        tail = "".join(transcript)[-8000:].replace("\r", "")
        raise RuntimeError(
            "first desktop did not reach supervised administrative idle; "
            f"guest tail:\n{tail}")

    vfs_offset = len("".join(transcript))
    send_command(process, "shell --vfs-probe")
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        drain(output, transcript)
        text = "".join(transcript)
        tail = text[vfs_offset:]
        if "Bad command or program file." in tail:
            raise RuntimeError("shell program loading failed after desktop exit")
        ready = tail.find("SHELL_VFS_NAMESPACE_OK")
        if ready >= 0 and SHELL_PROMPT in tail[ready:]:
            break
        time.sleep(0.02)
    else:
        raise RuntimeError("Ring-3 shell/VFS did not survive desktop exit")

    relaunch_offset = len("".join(transcript))
    send_command(process, "desktop")
    deadline = time.monotonic() + 90.0
    while time.monotonic() < deadline:
        drain(output, transcript)
        text = "".join(transcript)
        tail = text[relaunch_offset:]
        if "Bad command or program file." in tail:
            raise RuntimeError("desktop command was rejected after clean exit")
        if ("REIST_GUI COMPOSITOR_RESTARTED" in tail or
                "REIST_GUI COMPOSITOR_DEGRADED" in tail):
            raise RuntimeError("desktop relaunch consumed recovery budget")
        resumed = tail.find("REIST_GUI COMPOSITOR_SESSION_STARTED epoch=")
        ready = tail.find("REIST_GUI COMPOSITOR_READY generation=")
        desktop = tail.find("DESKTOP_OK")
        if resumed >= 0 and resumed < ready < desktop:
            break
        time.sleep(0.02)
    else:
        raise RuntimeError("second supervised desktop did not become ready")

    second_exit_offset = len("".join(transcript))
    send_desktop_exit_click(process)
    deadline = time.monotonic() + 45.0
    while time.monotonic() < deadline:
        drain(output, transcript)
        text = "".join(transcript)
        tail = text[second_exit_offset:]
        if text.count("DESKTOP_EXIT_OK") < 2:
            time.sleep(0.02)
            continue
        if "REIST_GUI COMPOSITOR_STOPPED epoch=" in tail:
            break
        time.sleep(0.02)
    else:
        raise RuntimeError("second desktop did not stop cleanly")

    shell_offset = len("".join(transcript))
    send_command(process, "help")
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        drain(output, transcript)
        text = "".join(transcript)
        tail = text[shell_offset:]
        help_offset = tail.find(SHELL_HELP_MARKER)
        if help_offset >= 0 and SHELL_PROMPT in tail[help_offset:]:
            print("runtime-desktop: PASS exit-vfs-relaunch-exit-shell")
            return 0
        time.sleep(0.02)
    raise RuntimeError("Ring-3 shell did not respond after second desktop exit")


def require_svga2d_console_lifecycle(text: str) -> None:
    if text.count("REIST_VIDEO SVGA2D_ACTIVE") < 2:
        raise RuntimeError("SVGA2D was not activated for the desktop")
    if text.count("REIST_VIDEO SVGA2D_INACTIVE") < 2:
        raise RuntimeError("SVGA2D did not restore VGA after both sessions")
    for marker in ("REIST_VIDEO SVGA2D_RECT_COPY_OK", "SVGA2D_READY",
                   "DESKTOP_EXIT_OK"):
        if marker not in text:
            raise RuntimeError(f"missing lifecycle marker: {marker}")
    first_inactive = text.index("REIST_VIDEO SVGA2D_INACTIVE")
    ready = text.index("REIST_VIDEO SVGA2D_READY")
    shell = text.index(SHELL_PROMPT)
    desktop_active = text.index("REIST_VIDEO SVGA2D_ACTIVE", shell)
    second_inactive = text.index("REIST_VIDEO SVGA2D_INACTIVE",
                                 desktop_active)
    desktop_exit = text.index("DESKTOP_EXIT_OK", second_inactive)
    if not (first_inactive < ready < shell < desktop_active <
            second_inactive < desktop_exit):
        raise RuntimeError("SVGA2D console lifecycle markers are out of order")


def run(qemu: pathlib.Path, image: pathlib.Path, screenshot: pathlib.Path,
        timeout: float, expect_failure: bool, render_probe: bool,
        surface_probe: bool, notepad_probe: bool, notepad_font_probe: bool,
        control_probe: bool, browser_probe: bool, trash_context_probe: bool,
        trash_confirm_probe: bool, trash_restore_probe: bool,
        explorer_scroll_probe: bool,
        explorer_views_probe: bool,
        shortcut_probe: bool,
        icon_layout_probe: bool,
        hover_probe: bool,
        supervised_probe: bool,
        guidemo_click_probe: bool,
        sound_probe: bool, metrics_log: pathlib.Path | None,
        vmware_vga: bool, smp: int, capture_only: bool = False) -> int:
    audio_capture = screenshot.with_name("runtime-desktop-audio.wav")
    if sound_probe and audio_capture.exists():
        audio_capture.unlink()
    dns_listener = None
    dns_connection = None
    dns_port = None
    if notepad_probe:
        dns_listener, dns_port = open_injection_listener()
        dns_listener.settimeout(10.0)
    command = qemu_command(
        qemu, image, memory="512M", vmware_vga=vmware_vga, smp=smp,
        nic="rtl8139" if notepad_probe else "none",
        injection_port=dns_port, hardware_entropy=notepad_probe,
        public_dns=notepad_probe)
    normal_lifecycle_probe = not any((
        expect_failure, render_probe, surface_probe, notepad_probe,
        notepad_font_probe, control_probe, browser_probe, trash_context_probe,
        trash_confirm_probe, trash_restore_probe, explorer_scroll_probe,
        explorer_views_probe, shortcut_probe, icon_layout_probe, hover_probe,
        guidemo_click_probe, sound_probe,
    ))
    if (guidemo_click_probe or hover_probe or shortcut_probe or
            icon_layout_probe or
            normal_lifecycle_probe):
        command.extend([
            "-device", "qemu-xhci,id=reistxhci",
            "-device", "usb-mouse,bus=reistxhci.0",
        ])
    if sound_probe:
        command.extend([
            "-audiodev",
            (f"wav,id=reistaudio,path={audio_capture},out.frequency=48000,"
             "out.channels=2,out.format=s16"),
            "-device", "intel-hda,msi=off,debug=1",
            "-device", "hda-output,audiodev=reistaudio,debug=1",
        ])
    if not vmware_vga:
        command.extend([
            "-device", "VGA,vgamem_mb=1" if expect_failure else "VGA"
        ])
    process = subprocess.Popen(command, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace", bufsize=0,
                               creationflags=QEMU_CREATION_FLAGS)
    if dns_listener is not None:
        try:
            dns_connection, _ = dns_listener.accept()
            dns_connection.settimeout(None)
            dns_connection.setsockopt(
                socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        finally:
            dns_listener.close()
    assert process.stdout is not None
    output: queue.Queue[str] = queue.Queue()
    finished = threading.Event()
    thread = threading.Thread(target=reader,
                              args=(process.stdout, output, finished), daemon=True)
    thread.start()
    transcript: list[str] = []
    deadline = time.monotonic() + timeout
    supervised_boot_detected = False
    hover_shell_probe_sent = False
    explorer_shell_probe_sent = False
    try:
        while time.monotonic() < deadline:
            drain(output, transcript)
            text = "".join(transcript)
            if ("REIST_GUI COMPOSITOR_READY" in text and
                    "DESKTOP_OK" in text):
                supervised_boot_detected = True
                break
            if SHELL_PROMPT in text:
                if notepad_probe:
                    download_offset = len(text)
                    reference_ip, resolve_error = \
                        resolve_notepad_reference_ipv4(deadline)
                    if resolve_error is not None or reference_ip is None or \
                            dns_connection is None:
                        raise RuntimeError(resolve_error or
                                           "Notepad DNS peer unavailable")
                    send_command(
                        process, "curl -o test.txt https://intracom.at")
                    dns_error = serve_dns_a_query(
                        dns_connection, deadline, TCP_TEST_TARGET,
                        TCP_TEST_MAC, NOTEPAD_REFERENCE_QUESTION,
                        reference_ip, True, False)
                    if dns_error is not None:
                        raise RuntimeError(
                            "intracom.at DNS injection failed: " + dns_error)
                    download_deadline = min(
                        deadline, time.monotonic() + 60.0)
                    while time.monotonic() < download_deadline:
                        drain(output, transcript)
                        download_tail = "".join(transcript)[download_offset:]
                        if SHELL_PROMPT in download_tail:
                            if ("Bad command or program file." in
                                    download_tail or "curl:" in download_tail):
                                raise RuntimeError(
                                    "curl could not download intracom.at; "
                                    "guest tail:\n" +
                                    download_tail[-2000:].replace("\r", ""))
                            break
                        time.sleep(0.02)
                    else:
                        raise RuntimeError(
                            "curl -o test.txt https://intracom.at timed out")
                    stat_offset = len("".join(transcript))
                    send_command(process, "stat test.txt")
                    while time.monotonic() < download_deadline:
                        drain(output, transcript)
                        stat_tail = "".join(transcript)[stat_offset:]
                        if SHELL_PROMPT in stat_tail:
                            if "Bad command or program file." in stat_tail:
                                raise RuntimeError(
                                    "downloaded test.txt could not be stated")
                            stat_lines = [line.strip() for line in
                                          stat_tail.replace("\r", "").split("\n")
                                          if line.strip() and
                                          "sendkey" not in line and
                                          "(qemu)" not in line]
                            print("notepad-reference-stat: " +
                                  " | ".join(stat_lines[-8:]))
                            break
                        time.sleep(0.02)
                    else:
                        raise RuntimeError("stat test.txt timed out")
                if render_probe:
                    command_name = "desktop.prg --render-probe"
                elif hover_probe:
                    command_name = (
                        "desktop" if supervised_probe
                        else "desktop.prg --hover-probe"
                    )
                elif surface_probe:
                    command_name = "desktop.prg --surface-probe"
                elif notepad_probe:
                    command_name = "desktop.prg --notepad-probe"
                elif notepad_font_probe:
                    command_name = "desktop.prg --notepad-font-probe"
                elif control_probe:
                    command_name = "desktop.prg --control-probe"
                elif browser_probe:
                    command_name = "desktop.prg --browser-probe"
                elif guidemo_click_probe:
                    command_name = "desktop.prg --guidemo-probe"
                elif sound_probe:
                    command_name = "desktop.prg --sound-probe"
                elif trash_context_probe:
                    command_name = "desktop.prg --trash-context-probe"
                elif trash_confirm_probe:
                    command_name = "desktop.prg --trash-confirm-probe"
                elif trash_restore_probe:
                    command_name = "desktop.prg --trash-restore-probe"
                elif explorer_scroll_probe:
                    command_name = "desktop.prg --explorer-scroll-probe"
                elif explorer_views_probe:
                    command_name = "desktop.prg --explorer-views-probe"
                elif shortcut_probe:
                    command_name = "desktop.prg --shortcut-probe"
                elif icon_layout_probe:
                    command_name = "desktop.prg --icon-layout-probe"
                else:
                    command_name = "desktop.prg"
                send_command(process, command_name)
                break
            time.sleep(0.02)
        else:
            drain(output, transcript)
            text = "".join(transcript)
            if ("REIST_GUI COMPOSITOR_READY" in text and
                    "DESKTOP_OK" in text):
                supervised_boot_detected = True
            else:
                try:
                    capture_screenshot(
                        process, screenshot, time.monotonic() + 2.0
                    )
                except RuntimeError:
                    pass
                try:
                    qemu_monitor_command(process, "info registers")
                    time.sleep(0.1)
                    drain(output, transcript)
                    text = "".join(transcript)
                except (BrokenPipeError, OSError, RuntimeError):
                    pass
                tail = text[-8000:].replace("\r", "")
                raise RuntimeError(
                    "supervised desktop or VGA shell prompt not observed; "
                    f"guest tail:\n{tail}")
        font_catalog_start = (surface_probe or notepad_probe or
                              notepad_font_probe or browser_probe or
                              shortcut_probe or
                              icon_layout_probe) or not any((
            expect_failure, render_probe, surface_probe, control_probe,
            browser_probe,
            trash_context_probe, trash_confirm_probe, trash_restore_probe,
            explorer_scroll_probe, explorer_views_probe,
            shortcut_probe, icon_layout_probe,
            hover_probe, guidemo_click_probe,
            sound_probe))
        desktop_deadline = time.monotonic() + (
            90.0 if font_catalog_start else 30.0)
        deadline = desktop_deadline
        while time.monotonic() < deadline:
            drain(output, transcript)
            text = "".join(transcript)
            if "DESKTOP_OK" in text:
                if expect_failure:
                    print("runtime-desktop: FAIL: unsupported mode was accepted",
                          file=sys.stderr)
                    return 1
                if supervised_boot_detected and control_probe:
                    time.sleep(0.2)
                    capture_screenshot(process, screenshot, deadline)
                    print("runtime-desktop: PASS supervised-generation")
                    return 0
                if shortcut_probe:
                    return run_shortcut_mouse_probe(
                        process, output, transcript, screenshot, deadline)
                if icon_layout_probe:
                    return run_icon_layout_mouse_probe(
                        process, output, transcript, screenshot, deadline)
                if hover_probe:
                    hover_ready_deadline = min(
                        deadline, time.monotonic() + 5.0
                    )
                    while time.monotonic() < hover_ready_deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_HOVER_PROBE_READY" in probe_text:
                            break
                        time.sleep(0.02)
                    else:
                        raise RuntimeError(
                            "desktop hover probe did not become ready"
                        )
                    capture_screenshot(process, screenshot, deadline)
                    send_hover_trajectory(process)
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        for failure in (
                                "DESKTOP_HOVER_FAIL",
                                "REIST_GUI COMPOSITOR_RESTARTED",
                                "REIST_GUI COMPOSITOR_DEGRADED"):
                            if failure in probe_text:
                                tail = probe_text[-8000:].replace("\r", "")
                                raise RuntimeError(
                                    "desktop hover probe failed: "
                                    f"{failure}; guest tail:\n{tail}"
                                )
                        if ("DESKTOP_HOVER_METRICS" in probe_text and
                                "DESKTOP_HOVER_OK" in probe_text and
                                "DESKTOP_EXIT_OK" in probe_text):
                            if (supervised_probe and
                                    "REIST_GUI COMPOSITOR_READY generation="
                                    not in probe_text):
                                raise RuntimeError(
                                    "hover probe bypassed compositor supervisor"
                                )
                            exit_offset = probe_text.index("DESKTOP_EXIT_OK")
                            if not hover_shell_probe_sent:
                                send_command(process, "help")
                                hover_shell_probe_sent = True
                                time.sleep(0.02)
                                continue
                            help_offset = probe_text.find(
                                SHELL_HELP_MARKER, exit_offset
                            )
                            prompt_offset = probe_text.find(
                                SHELL_PROMPT, help_offset
                            ) if help_offset >= 0 else -1
                            if prompt_offset < 0:
                                time.sleep(0.02)
                                continue
                            if "DESKTOP_MOUSE_OK" not in probe_text:
                                raise RuntimeError(
                                    "hover metrics did not follow USB input"
                                )
                            metrics, metric_line = parse_hover_metrics(
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
                                "runtime-desktop-hover: PASS "
                                f"hover_max_ms={metrics['max_ms']} "
                                "pointer_gap_max_ms="
                                f"{metrics['pointer_max_gap_ms']} "
                                "pointer_latency_max_ms="
                                f"{metrics['pointer_latency_max_ms']} "
                                "pointer_call_max_ms="
                                f"{metrics['pointer_call_max_ms']}"
                            )
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "desktop hover probe did not finish; guest tail:\n" +
                        "".join(transcript)[-8000:].replace("\r", "")
                    )
                if guidemo_click_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_GUIDEMO_FAIL" in probe_text:
                            raise RuntimeError("GUIDEMO probe launch failed")
                        if ("DESKTOP_GUIDEMO_OK" in probe_text and
                                "GUIDEMO_SURFACE_READY" in probe_text):
                            break
                        time.sleep(0.02)
                    else:
                        raise RuntimeError(
                            "GUIDEMO Surface did not become visible"
                        )
                    capture_screenshot(process, screenshot, deadline)
                    for command in (
                            "mouse_move -100 -258",
                            "mouse_button 1", "mouse_button 0"):
                        qemu_monitor_command(process, command)
                        time.sleep(0.08)
                    tab_deadline = min(deadline, time.monotonic() + 5.0)
                    while time.monotonic() < tab_deadline:
                        drain(output, transcript)
                        if "GUIDEMO_INTERACTION_OK" in "".join(transcript):
                            break
                        time.sleep(0.02)
                    else:
                        drain(output, transcript)
                        pointer_trace = "\n".join(
                            line for line in "".join(transcript).splitlines()
                            if "GUIDEMO" in line
                        )[-4000:]
                        raise RuntimeError(
                            "physical USB click did not activate GUIDEMO tab; "
                            f"pointer trace:\n{pointer_trace}"
                        )
                    for command in (
                            "mouse_move -132 -30",
                            "mouse_button 1", "mouse_button 0",
                            "mouse_move 68 27",
                            "mouse_button 1", "mouse_button 0"):
                        qemu_monitor_command(process, command)
                        time.sleep(0.08)
                    menu_deadline = min(deadline, time.monotonic() + 5.0)
                    while time.monotonic() < menu_deadline:
                        drain(output, transcript)
                        if "GUIDEMO_MENU_INTERACTION_OK" in \
                                "".join(transcript):
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-guidemo-click: PASS")
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "physical USB clicks did not activate GUIDEMO menu; "
                        "inspect DESKTOP_GUIDEMO_POINTER trace"
                    )
                if sound_probe:
                    audio_sound_bound = False
                    audio_client_bound = False
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if (not audio_sound_bound and
                                "DESKTOP_AUDIO_STAGE sound-bound" in
                                probe_text):
                            audio_sound_bound = True
                            deadline = time.monotonic() + 15.0
                        if (not audio_client_bound and
                                "DESKTOP_AUDIO_STAGE client-bound" in
                                probe_text):
                            audio_client_bound = True
                            deadline = time.monotonic() + 15.0
                        for failure in (
                                "REIST_GUI COMPOSITOR_RESTARTED",
                                "REIST_GUI COMPOSITOR_DEGRADED",
                                "DESKTOP_AUDIO_FAIL",
                                "SOUNDPLAYER_AUDIO_FAIL"):
                            if failure in probe_text:
                                time.sleep(0.5)
                                drain(output, transcript)
                                probe_text = "".join(transcript)
                                tail = probe_text[-8000:].replace("\r", "")
                                raise RuntimeError(
                                    "audio Surface lifecycle failed: "
                                    f"{failure}; guest tail:\n{tail}"
                                )
                        if ("SOUNDPLAYER_PLAYBACK_OK" in probe_text and
                                "GUIDEMO_SURFACE_READY" in probe_text and
                                "GUIDEMO_INTERACTION_OK" in probe_text and
                                "DESKTOP_AUDIO_HEARTBEAT_OK" in probe_text):
                            capture_screenshot(process, screenshot, deadline)
                            stop_process(process)
                            finalized, detail = finalize_qemu_wave(audio_capture)
                            if not finalized:
                                raise RuntimeError(detail)
                            valid, detail = validate_system_sound_wave(
                                audio_capture)
                            if not valid:
                                raise RuntimeError(detail)
                            print(f"runtime-desktop-audio: PASS {detail}")
                            return 0
                        time.sleep(0.02)
                    tail = "".join(transcript)[-2000:].replace("\r", "")
                    raise RuntimeError(
                        "Sound Player did not cross the compositor heartbeat "
                        f"deadline; guest tail:\n{tail}"
                    )
                if render_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if ("DESKTOP_METRICS" in probe_text and
                                "DESKTOP_EXIT_OK" in probe_text):
                            if SHELL_PROMPT in probe_text:
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
                                if vmware_vga:
                                    require_svga2d_console_lifecycle(probe_text)
                                print(
                                    "runtime-desktop-metrics: PASS "
                                    f"full_max_ms={metrics['full_max_ms']} "
                                    f"drag_max_ms={metrics['drag_max_ms']} "
                                    f"resize_max_ms={metrics['resize_max_ms']}"
                                )
                                return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "desktop render probe did not preserve the VGA shell; "
                        "guest tail:\n" +
                        "".join(transcript)[-8000:].replace("\r", "")
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
                        if ("notepad: Surface-Frame dauerhaft" in probe_text or
                                "notepad: Surface-Frame konnte" in probe_text or
                                "notepad: Surface konnte" in probe_text):
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
                                "NOTEPAD_SURFACE_HOVER_READY" in probe_text and
                                "NOTEPAD_SURFACE_DIALOG_READY" in probe_text and
                                "NOTEPAD_SURFACE_RESIZE_OK" in probe_text):
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
                    notepad_surface_seen = False
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if (not notepad_surface_seen and
                                "NOTEPAD_SURFACE_READY" in probe_text):
                            notepad_surface_seen = True
                            deadline = time.monotonic() + 60.0
                        if "DESKTOP_NOTEPAD_FAIL" in probe_text:
                            raise RuntimeError("Notepad probe launch failed")
                        if "NOTEPAD_PIECE_DOCUMENT_FAIL" in probe_text:
                            failures = re.findall(
                                r"NOTEPAD_GLOBAL_WRAP_FAIL[^\r\n]*",
                                probe_text)
                            marker = failures[-1] if failures else re.findall(
                                r"NOTEPAD_PIECE_DOCUMENT_FAIL[^\r\n]*",
                                probe_text)[-1]
                            raise RuntimeError(
                                f"Large Notepad document failed: {marker}; "
                                "guest tail:\n" +
                                probe_text[-4000:].replace("\r", ""))
                        if ("notepad: Surface-Frame dauerhaft" in probe_text or
                                "notepad: Surface-Frame konnte" in probe_text or
                                "notepad: Surface konnte" in probe_text):
                            marker = re.findall(
                                r"notepad: Surface[^\r\n]*", probe_text
                            )[-1]
                            raise RuntimeError(
                                f"Editor Surface failed: {marker}"
                            )
                        if ("NOTEPAD_SURFACE_READY" in probe_text and
                                "NOTEPAD_SURFACE_DOCUMENT_READY" in
                                probe_text and
                                "NOTEPAD_REFERENCE_DOCUMENT_READY" in
                                probe_text and
                                "NOTEPAD_DOCUMENT_NAVIGATION_READY" in
                                probe_text and
                                "NOTEPAD_VIRTUAL_WRAP_READY" in probe_text and
                                "NOTEPAD_GLOBAL_WRAP_STABLE" in probe_text and
                                "NOTEPAD_PIECE_DOCUMENT_READY" in probe_text):
                            time.sleep(0.2)
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-notepad: PASS")
                            return 0
                        time.sleep(0.02)
                    tail = "".join(transcript)[-4000:].replace("\r", "")
                    raise RuntimeError(
                        "Notepad did not publish a visible document window; "
                        f"guest tail:\n{tail}"
                    )
                if notepad_font_probe:
                    font_surface_seen = False
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if (not font_surface_seen and
                                "NOTEPAD_SURFACE_READY" in probe_text):
                            font_surface_seen = True
                            deadline = time.monotonic() + 90.0
                        for failure in (
                                "DESKTOP_NOTEPAD_FAIL",
                                "NOTEPAD_PIECE_DOCUMENT_FAIL",
                                "NOTEPAD_FONT_SELECTION_FAIL",
                                "DESKTOP_EDITOR_FONT_FALLBACK"):
                            if failure in probe_text:
                                marker = re.findall(
                                    re.escape(failure) + r"[^\r\n]*",
                                    probe_text)[-1]
                                raise RuntimeError(
                                    f"Notepad font probe failed: {marker}")
                        selections = re.findall(
                            r"NOTEPAD_FONT_SELECTION_OK family=(\d+) "
                            r"height=(10|28) width=(\d+)", probe_text)
                        expected = {
                            (str(family), str(height))
                            for family in range(1, 6)
                            for height in (10, 28)
                        }
                        observed = {(family, height)
                                    for family, height, _ in selections}
                        if ("NOTEPAD_FONT_SELECTION_READY" in probe_text and
                                "NOTEPAD_PIECE_DOCUMENT_READY" in probe_text and
                                expected == observed):
                            time.sleep(0.2)
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-notepad-fonts: PASS")
                            return 0
                        time.sleep(0.02)
                    tail = "".join(transcript)[-5000:].replace("\r", "")
                    raise RuntimeError(
                        "Notepad font catalog was not exercised completely; "
                        f"guest tail:\n{tail}"
                    )
                if control_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_CONTROL_FAIL" in probe_text:
                            raise RuntimeError("Control Panel probe launch failed")
                        if ("DESKTOP_CONTROL_OK" in probe_text and
                                "CONTROL_PANEL_READY" in probe_text):
                            time.sleep(0.2)
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-control: PASS")
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "Control Panel did not publish a visible window"
                    )
                if browser_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if ("DESKTOP_BROWSER_FAIL" in probe_text or
                                "BROWSER_PROBE_FAIL" in probe_text):
                            raise RuntimeError("Browser probe failed")
                        required = (
                            "DESKTOP_BROWSER_OK", "BROWSER_RENDER_OK",
                            "BROWSER_SCROLL_OK", "BROWSER_LINK_OK",
                            "BROWSER_RELOAD_OK", "BROWSER_RELOAD_PAINTED",
                        )
                        if all(marker in probe_text for marker in required):
                            capture_screenshot(process, screenshot, deadline)
                            break
                        time.sleep(0.02)
                    else:
                        raise RuntimeError(
                            "Browser did not render, scroll, follow and reload"
                        )
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        if "BROWSER_CLOSE_OK" in "".join(transcript):
                            print("runtime-desktop-browser: PASS")
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError("Browser did not close cleanly")
                if trash_context_probe or trash_confirm_probe:
                    ready_marker = (
                        "DESKTOP_TRASH_CONTEXT_READY"
                        if trash_context_probe
                        else "DESKTOP_TRASH_CONFIRM_READY"
                    )
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_TRASH_PROBE_FAIL" in probe_text:
                            raise RuntimeError(
                                "Trash documentation probe setup failed"
                            )
                        if ready_marker in probe_text:
                            time.sleep(0.2)
                            capture_screenshot(process, screenshot, deadline)
                            print(
                                "runtime-desktop-trash-context: PASS"
                                if trash_context_probe else
                                "runtime-desktop-trash-confirm: PASS"
                            )
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "Trash documentation state was not published"
                    )
                if trash_restore_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        for failure in (
                                "DESKTOP_TRASH_PROBE_FAIL",
                                "REIST_GUI COMPOSITOR_RESTARTED",
                                "REIST_GUI COMPOSITOR_DEGRADED"):
                            if failure in probe_text:
                                raise RuntimeError(
                                    f"Trash restore probe failed: {failure}"
                                )
                        metadata_offset = probe_text.find(
                            "DESKTOP_TRASH_VFS_METADATA_OK"
                        )
                        restore_offset = probe_text.find(
                            "DESKTOP_TRASH_RESTORE_READY"
                        )
                        desktop_offset = probe_text.find("DESKTOP_OK")
                        if (metadata_offset >= 0 and
                                metadata_offset < restore_offset <
                                desktop_offset):
                            capture_screenshot(process, screenshot, deadline)
                            print("runtime-desktop-trash-restore: PASS")
                            return 0
                        time.sleep(0.02)
                    raise RuntimeError(
                        "Trash metadata object did not complete move/restore"
                    )
                if explorer_scroll_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_EXPLORER_SCROLL_FAIL" in probe_text:
                            marker = re.findall(
                                r"DESKTOP_EXPLORER_SCROLL_FAIL[^\r\n]*",
                                probe_text)[-1]
                            raise RuntimeError(
                                f"Explorer scroll probe failed: {marker}")
                        if ("DESKTOP_EXPLORER_SCROLL_OK" in probe_text and
                                "DESKTOP_EXIT_OK" in probe_text):
                            exit_offset = probe_text.index("DESKTOP_EXIT_OK")
                            if not explorer_shell_probe_sent:
                                send_command(process, "help")
                                explorer_shell_probe_sent = True
                                time.sleep(0.02)
                                continue
                            help_offset = probe_text.find(
                                SHELL_HELP_MARKER, exit_offset
                            )
                            prompt_offset = probe_text.find(
                                SHELL_PROMPT, help_offset
                            ) if help_offset >= 0 else -1
                            if prompt_offset >= 0:
                                print("runtime-desktop-explorer-scroll: PASS")
                                return 0
                        time.sleep(0.02)
                    tail = "".join(transcript)[-8000:].replace("\r", "")
                    raise RuntimeError(
                        "Explorer scroll probe did not restore the shell; "
                        f"guest tail:\n{tail}"
                    )
                if explorer_views_probe:
                    while time.monotonic() < deadline:
                        drain(output, transcript)
                        probe_text = "".join(transcript)
                        if "DESKTOP_EXPLORER_VIEWS_FAIL" in probe_text:
                            marker = re.findall(
                                r"DESKTOP_EXPLORER_VIEWS_FAIL[^\r\n]*",
                                probe_text)[-1]
                            raise RuntimeError(
                                f"Explorer views probe failed: {marker}")
                        if ("DESKTOP_EXPLORER_VIEWS_OK" in probe_text and
                                "DESKTOP_EXIT_OK" in probe_text):
                            exit_offset = probe_text.index("DESKTOP_EXIT_OK")
                            if not explorer_shell_probe_sent:
                                send_command(process, "help")
                                explorer_shell_probe_sent = True
                                time.sleep(0.02)
                                continue
                            help_offset = probe_text.find(
                                SHELL_HELP_MARKER, exit_offset
                            )
                            prompt_offset = probe_text.find(
                                SHELL_PROMPT, help_offset
                            ) if help_offset >= 0 else -1
                            if prompt_offset >= 0:
                                print("runtime-desktop-explorer-views: PASS")
                                return 0
                        time.sleep(0.02)
                    tail = "".join(transcript)[-8000:].replace("\r", "")
                    raise RuntimeError(
                        "Explorer views probe did not restore the shell; "
                        f"guest tail:\n{tail}"
                    )
                # The marker is emitted immediately before the single
                # backbuffer render so it is overwritten by the desktop.
                # Give the guest a bounded interval to finish that frame
                # before capturing it or injecting Escape.
                time.sleep(1.5 if capture_only else 0.2)
                capture_screenshot(process, screenshot, deadline)
                if capture_only:
                    print("runtime-desktop-capture: PASS")
                    return 0
                if supervised_boot_detected:
                    print("runtime-desktop: PASS supervised-generation")
                    return 0
                return run_desktop_relaunch_probe(
                    process, output, transcript)
            if "desktop: Grafikmodus nicht verfuegbar" in text:
                tail = text[-12000:].replace("\r", "")
                raise RuntimeError(
                    "native runtime graphics activation failed; "
                    f"guest tail:\n{tail}"
                )
            time.sleep(0.02)
        drain(output, transcript)
        tail = "".join(transcript)[-8000:].replace("\r", "")
        raise RuntimeError(f"DESKTOP_OK marker not observed; guest tail:\n{tail}")
    finally:
        if dns_connection is not None:
            dns_connection.close()
        stop_process(process)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", type=pathlib.Path, required=True)
    parser.add_argument("--image", type=pathlib.Path, required=True)
    parser.add_argument("--screenshot", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--expect-failure", action="store_true")
    parser.add_argument("--render-probe", action="store_true")
    parser.add_argument("--surface-probe", action="store_true")
    parser.add_argument("--notepad-probe", action="store_true")
    parser.add_argument("--notepad-font-probe", action="store_true")
    parser.add_argument("--control-probe", action="store_true")
    parser.add_argument("--browser-probe", action="store_true")
    parser.add_argument("--trash-context-probe", action="store_true")
    parser.add_argument("--trash-confirm-probe", action="store_true")
    parser.add_argument("--trash-restore-probe", action="store_true")
    parser.add_argument("--explorer-scroll-probe", action="store_true")
    parser.add_argument("--explorer-views-probe", action="store_true")
    parser.add_argument("--shortcut-probe", action="store_true")
    parser.add_argument("--icon-layout-probe", action="store_true")
    parser.add_argument("--sound-probe", action="store_true")
    parser.add_argument("--guidemo-click-probe", action="store_true")
    parser.add_argument("--hover-probe", action="store_true")
    parser.add_argument("--supervised-probe", action="store_true")
    parser.add_argument("--metrics-log", type=pathlib.Path)
    parser.add_argument("--vmware-vga", action="store_true")
    parser.add_argument("--smp", type=int, default=1)
    args = parser.parse_args()
    if sum((args.expect_failure, args.render_probe, args.surface_probe,
            args.notepad_probe, args.notepad_font_probe, args.control_probe,
            args.browser_probe,
            args.trash_context_probe, args.trash_confirm_probe,
            args.trash_restore_probe,
            args.explorer_scroll_probe, args.explorer_views_probe,
            args.shortcut_probe, args.icon_layout_probe,
            args.sound_probe, args.guidemo_click_probe,
            args.hover_probe)) > 1:
        parser.error("desktop probe modes are mutually exclusive")
    if (args.metrics_log is not None and
            not (args.render_probe or args.hover_probe)):
        parser.error("--metrics-log requires --render-probe or --hover-probe")
    if args.supervised_probe and not args.hover_probe:
        parser.error("--supervised-probe requires --hover-probe")
    if args.smp < 1 or args.smp > 16:
        parser.error("--smp must be in 1..16")
    try:
        return run(args.qemu, args.image, args.screenshot, args.timeout,
                   args.expect_failure, args.render_probe,
                   args.surface_probe, args.notepad_probe,
                   args.notepad_font_probe, args.control_probe,
                   args.browser_probe,
                   args.trash_context_probe, args.trash_confirm_probe,
                   args.trash_restore_probe,
                   args.explorer_scroll_probe, args.explorer_views_probe,
                   args.shortcut_probe, args.icon_layout_probe,
                   args.hover_probe,
                   args.supervised_probe,
                   args.guidemo_click_probe, args.sound_probe,
                   args.metrics_log, args.vmware_vga, args.smp)
    except (OSError, RuntimeError) as error:
        print(f"runtime-desktop: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
