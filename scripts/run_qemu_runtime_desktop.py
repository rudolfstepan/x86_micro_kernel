"""Prove desktop.prg activates native graphics after a VGA text boot."""

from __future__ import annotations

import argparse
import pathlib
import queue
import re
import subprocess
import sys
import threading
import time

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


def run(qemu: pathlib.Path, image: pathlib.Path, screenshot: pathlib.Path,
        timeout: float, expect_failure: bool, render_probe: bool,
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
                    else "desktop.prg"
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
                # The marker is emitted immediately before the single
                # backbuffer render so it is overwritten by the desktop.
                # Give the guest a bounded interval to finish that frame
                # before capturing it or injecting Escape.
                time.sleep(0.2)
                if process.stdin is not None:
                    process.stdin.write(QEMU_MUX_SWITCH)
                    process.stdin.flush()
                    time.sleep(0.05)
                    process.stdin.write(f"screendump {screenshot}\n")
                    process.stdin.flush()
                    time.sleep(0.05)
                    process.stdin.write(QEMU_MUX_SWITCH)
                    process.stdin.flush()
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
    parser.add_argument("--metrics-log", type=pathlib.Path)
    args = parser.parse_args()
    if args.expect_failure and args.render_probe:
        parser.error("--expect-failure and --render-probe are exclusive")
    if args.metrics_log is not None and not args.render_probe:
        parser.error("--metrics-log requires --render-probe")
    try:
        return run(args.qemu, args.image, args.screenshot, args.timeout,
                   args.expect_failure, args.render_probe, args.metrics_log)
    except (OSError, RuntimeError) as error:
        print(f"runtime-desktop: FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
