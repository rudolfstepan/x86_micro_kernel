#!/usr/bin/env python3
"""Prove bounded REIST PCI audio playback and capture real PCM output."""

from __future__ import annotations

import argparse
import subprocess
import threading
import time
import wave
from pathlib import Path

try:
    from .run_qemu_smoke import (
        inject_ps2_command, qemu_command, qemu_monitor_command,
    )
except ImportError:  # Direct script execution.
    from run_qemu_smoke import (
        inject_ps2_command, qemu_command, qemu_monitor_command,
    )


SHELL_PROMPT = "C:\\>"
AUDIO_READY = "REIST_AUDIO SERVICE_READY"
AUDIOINFO_OK = "REIST audio: ready"
AUDIOTEST_OK = "Audio test complete."
AUDIO_TEST_CYCLES = 5
FAIL_MARKERS = (
    "PANIC:", "KERNEL ASSERTION FAILED", "Kernel exception:",
    "REIST_AUDIO DRIVER_DEGRADED", "REIST_AUDIO SERVICE_DEGRADED",
)

QEMU_WAVE_HEADER_BYTES = 44


def audio_qemu_command(qemu: Path, image: Path, wav_path: Path) -> list[str]:
    """Return the deterministic virtual Intel-HDA plus WAV backend command."""
    command = qemu_command(qemu, image, nic="none")
    command.extend([
        "-audiodev",
        (f"wav,id=reistaudio,path={wav_path},out.frequency=48000,"
         "out.channels=2,out.format=s16"),
        # Level-one device diagnostics are bounded and expose the parsed BDL,
        # stream transitions and codec setup when the runtime proof fails.
        "-device", "intel-hda,msi=off,debug=1",
        "-device", "hda-output,audiodev=reistaudio,debug=1",
    ])
    return command


def validate_wave(path: Path) -> tuple[bool, str]:
    """Require a stereo 16-bit capture containing at least one audible sample."""
    if not path.is_file():
        return False, "QEMU did not create the audio capture"
    try:
        with wave.open(str(path), "rb") as capture:
            channels = capture.getnchannels()
            width = capture.getsampwidth()
            frames = capture.getnframes()
            payload = capture.readframes(frames)
    except (OSError, EOFError, wave.Error) as error:
        return False, f"invalid WAV capture: {error}"
    if channels != 2 or width != 2 or frames == 0:
        return False, (f"unexpected WAV format channels={channels} "
                       f"width={width} frames={frames}")
    if not any(payload):
        return False, "WAV capture contains silence only"
    return True, f"stereo S16 capture frames={frames} bytes={len(payload)}"


def finalize_qemu_wave(path: Path) -> tuple[bool, str]:
    """Finalize only QEMU's canonical zero-length WAV header after shutdown.

    Some QEMU builds leave the RIFF and data lengths at zero when the VM exits
    through the multiplexed HMP monitor even though all PCM bytes were flushed.
    The repair is deliberately narrow: existing nonzero sizes and noncanonical
    files are never rewritten.
    """
    if not path.is_file():
        return False, "QEMU did not create the audio capture"
    size = path.stat().st_size
    if size < QEMU_WAVE_HEADER_BYTES or size - 8 > 0xFFFFFFFF:
        return False, f"invalid QEMU WAV size: {size}"
    with path.open("r+b") as capture:
        header = capture.read(QEMU_WAVE_HEADER_BYTES)
        if (header[0:4] != b"RIFF" or header[8:12] != b"WAVE" or
                header[12:16] != b"fmt " or header[36:40] != b"data"):
            return False, "QEMU capture has a noncanonical WAV header"
        riff_size = int.from_bytes(header[4:8], "little")
        data_size = int.from_bytes(header[40:44], "little")
        if riff_size != 0 or data_size != 0:
            return True, "WAV header already finalized"
        payload_size = size - QEMU_WAVE_HEADER_BYTES
        capture.seek(4)
        capture.write((size - 8).to_bytes(4, "little"))
        capture.seek(40)
        capture.write(payload_size.to_bytes(4, "little"))
        capture.flush()
    return True, f"finalized QEMU WAV payload bytes={payload_size}"


def wait_for(transcript: list[str], marker: str, deadline: float) -> bool:
    while time.monotonic() < deadline:
        if marker in "".join(transcript):
            return True
        time.sleep(0.05)
    return marker in "".join(transcript)


def wait_for_count(transcript: list[str], marker: str, count: int,
                   deadline: float) -> bool:
    """Wait for a repeated marker without accepting an earlier occurrence."""
    while time.monotonic() < deadline:
        if "".join(transcript).count(marker) >= count:
            return True
        time.sleep(0.05)
    return "".join(transcript).count(marker) >= count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--wav", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    if (not args.qemu.is_file() or not args.image.is_file() or
            args.timeout <= 5.0):
        return 2
    args.wav.parent.mkdir(parents=True, exist_ok=True)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    if args.wav.exists():
        args.wav.unlink()

    process = subprocess.Popen(
        audio_qemu_command(args.qemu, args.image, args.wav),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding="utf-8",
        errors="replace", bufsize=1,
    )
    transcript: list[str] = []

    def read_output() -> None:
        assert process.stdout is not None
        # Serial prompts are not newline terminated.  Reading large buffered
        # chunks can therefore hide the final prompt until QEMU exits.
        for chunk in iter(lambda: process.stdout.read(1), ""):
            transcript.append(chunk)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    deadline = time.monotonic() + args.timeout
    detail = ""
    try:
        if not wait_for(transcript, SHELL_PROMPT, deadline):
            detail = "userspace shell prompt was not reached"
        elif not wait_for(transcript, AUDIO_READY, deadline):
            detail = "supervised audio service did not become ready"
        else:
            inject_ps2_command(process, "audioinfo")
            if not wait_for(transcript, AUDIOINFO_OK, deadline):
                detail = "audioinfo did not confirm the PCM service"
            else:
                # More cycles than the service restart budget prove that
                # normal short-lived clients rotate their endpoint generation
                # without being misclassified as service failures.
                for cycle in range(1, AUDIO_TEST_CYCLES + 1):
                    inject_ps2_command(process, "audiotest")
                    if not wait_for_count(
                            transcript, AUDIOTEST_OK, cycle, deadline):
                        detail = f"audiotest cycle {cycle} did not complete"
                        break
    finally:
        if process.poll() is None:
            try:
                # Let the WAV backend finalize RIFF/data lengths and flush
                # samples before falling back to forced host termination.
                qemu_monitor_command(process, "quit")
            except (BrokenPipeError, OSError):
                pass
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5.0)
        reader.join(timeout=2.0)
        complete = "".join(transcript)
        args.log.write_text(complete, encoding="utf-8")

    if not detail:
        for marker in FAIL_MARKERS:
            if marker in complete:
                detail = f"guest reported failure marker: {marker}"
                break
    if not detail:
        finalized, wave_detail = finalize_qemu_wave(args.wav)
        valid = False
        if finalized:
            valid, wave_detail = validate_wave(args.wav)
        if not valid:
            detail = wave_detail
        else:
            print(f"pci-audio: PASS {wave_detail}")
            return 0
    print(f"pci-audio: FAIL {detail}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
