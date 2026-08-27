#!/usr/bin/env python3
"""Prove bounded REIST PCI audio playback and capture real PCM output."""

from __future__ import annotations

import argparse
import array
import itertools
import shutil
import subprocess
import sys
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
DMA_POOL_READY = (
    "REIST_DRIVER DIAGNOSTIC name=hda-ring3 value=D0114000"
)
AUDIOINFO_OK = "REIST audio: ready"
AUDIOTEST_OK = "Audio test complete."
AUDIO_TEST_CYCLES = 5
FAIL_MARKERS = (
    "PANIC:", "KERNEL ASSERTION FAILED", "Kernel exception:",
    "REIST_AUDIO DRIVER_DEGRADED", "REIST_AUDIO SERVICE_DEGRADED",
)

QEMU_WAVE_HEADER_BYTES = 44
PITCH_ANALYSIS_FRAMES = 24000
PITCH_SEARCH_MIN_HZ = 360
PITCH_SEARCH_MAX_HZ = 520
PITCH_ACCEPT_MIN_HZ = 435.0
PITCH_ACCEPT_MAX_HZ = 445.0
ROOT = Path(__file__).resolve().parents[1]


def default_qemu_path() -> Path | None:
    """Resolve the same bounded QEMU candidates as the runtime wrapper."""
    executable = shutil.which("qemu-system-i386")
    if executable:
        return Path(executable)
    for candidate in (
            Path(r"C:\tmp\qemu-portable\qemu-system-i386.exe"),
            Path(r"C:\Program Files\qemu\qemu-system-i386.exe"),
            Path(r"C:\msys64\mingw64\bin\qemu-system-i386.exe")):
        if candidate.is_file():
            return candidate
    return None


def audio_qemu_command(qemu: Path, image: Path, wav_path: Path,
                       smp: int = 1) -> list[str]:
    """Return the deterministic virtual Intel-HDA plus WAV backend command."""
    command = qemu_command(qemu, image, nic="none", smp=smp)
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


def estimate_pitch_hz(samples, rate: int) -> float | None:
    """Estimate the fundamental period with bounded difference correlation.

    QEMU's HDA model can introduce a one-sample discontinuity when the cyclic
    BDL wraps.  Counting zero crossings turns each sparse discontinuity into a
    false period.  Comparing samples at candidate period lags keeps those
    isolated boundary artifacts from biasing the 440-Hz measurement.
    """
    count = min(len(samples), PITCH_ANALYSIS_FRAMES)
    if rate <= 0 or count < PITCH_ANALYSIS_FRAMES:
        return None
    minimum_lag = max(1, rate // PITCH_SEARCH_MAX_HZ)
    maximum_lag = (rate + PITCH_SEARCH_MIN_HZ - 1) // PITCH_SEARCH_MIN_HZ
    best_lag = 0
    best_total = 0
    best_count = 1
    for lag in range(minimum_lag, maximum_lag + 1):
        compared = count - lag
        total = sum(abs(samples[index] - samples[index - lag])
                    for index in range(lag, count))
        if (best_lag == 0 or
                total * best_count < best_total * compared):
            best_lag = lag
            best_total = total
            best_count = compared
    if best_lag == 0:
        return None
    return rate / best_lag


def validate_wave(path: Path) -> tuple[bool, str]:
    """Require gap-free stereo S16 output at the requested 440-Hz pitch."""
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
    if (channels != 2 or width != 2 or rate != 48000 or frames == 0):
        return False, (f"unexpected WAV format channels={channels} "
                       f"width={width} rate={rate} frames={frames}")
    if not any(payload):
        return False, "WAV capture contains silence only"
    samples = array.array("h")
    samples.frombytes(payload)
    if sys.byteorder != "little":
        samples.byteswap()
    left = samples[0::channels]
    first = next((index for index, value in enumerate(left) if value != 0), -1)
    last = next((len(left) - 1 - index for index, value in
                 enumerate(reversed(left)) if value != 0), -1)
    if first < 0 or last <= first:
        return False, "WAV capture has no bounded active interval"
    active = left[first:last + 1]
    estimated_hz = estimate_pitch_hz(active, rate)
    if estimated_hz is None:
        return False, f"audio interval too short: {len(active)} frames"
    if (estimated_hz < PITCH_ACCEPT_MIN_HZ or
            estimated_hz > PITCH_ACCEPT_MAX_HZ):
        return False, f"unexpected test-tone pitch: {estimated_hz:.1f} Hz"
    longest_zero_run = max(
        (sum(1 for _ in group) for zero, group in itertools.groupby(
            active, key=lambda value: value == 0) if zero), default=0)
    if longest_zero_run > rate // 1000:
        return False, ("audio capture contains an interior underrun of "
                       f"{longest_zero_run} frames")
    return True, (f"stereo S16 capture frames={frames} "
                  f"tone={estimated_hz:.1f}Hz max-gap={longest_zero_run}")


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
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--image", type=Path,
                        default=ROOT / "build" / "reist-os.img")
    parser.add_argument("--wav", type=Path,
                        default=ROOT / "build" / "qemu-pci-audio.wav")
    parser.add_argument("--log", type=Path,
                        default=ROOT / "build" / "qemu-pci-audio.log")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--smp", type=int, choices=(1, 2, 3, 4), default=1)
    parser.add_argument("--expect-hda-smp-restart", action="store_true")
    parser.add_argument("--expect-audio-service-smp", action="store_true")
    parser.add_argument("--expect-audio-service-smp-restart", action="store_true")
    args = parser.parse_args()
    if args.qemu is None:
        args.qemu = default_qemu_path()
    if args.qemu is None:
        print("pci-audio: FAIL qemu-system-i386 was not found")
        return 2
    if (not args.qemu.is_file() or not args.image.is_file() or
            args.timeout <= 5.0):
        return 2
    args.wav.parent.mkdir(parents=True, exist_ok=True)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    if args.wav.exists():
        args.wav.unlink()

    process = subprocess.Popen(
        audio_qemu_command(args.qemu, args.image, args.wav, smp=args.smp),
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
        elif not wait_for(transcript, DMA_POOL_READY, deadline):
            detail = "mediated DMA pool diagnostics were not observed"
        elif args.expect_audio_service_smp_restart and not wait_for(
                transcript, "REIST_AUDIO SERVICE_TIMEOUT_ARMED epoch=", deadline):
            detail = "audio-service AP heartbeat fault was not armed"
        elif args.expect_audio_service_smp_restart and not wait_for(
                transcript, "REIST_AUDIO SERVICE_RESTARTED", deadline):
            detail = "audio service was not restarted after heartbeat timeout"
        elif args.expect_audio_service_smp_restart and not wait_for_count(
                transcript, AUDIO_READY, 2, deadline):
            detail = "replacement audio service did not become ready"
        elif args.expect_hda_smp_restart and not wait_for(
                transcript, "REIST_AUDIO HDA_TIMEOUT_ARMED epoch=", deadline):
            detail = "HDA AP heartbeat fault was not armed"
        elif args.expect_hda_smp_restart and not wait_for(
                transcript, "REIST_AUDIO DRIVER_RESTARTED", deadline):
            detail = "HDA driver was not restarted after its heartbeat timeout"
        elif args.expect_hda_smp_restart and not wait_for_count(
                transcript, "REIST_DRIVER READY name=hda-ring3", 2, deadline):
            detail = "replacement HDA generation did not become healthy"
        else:
            if args.expect_hda_smp_restart:
                # A service with no client performs no ambient reconnect
                # polling. The first bounded request discovers the stale
                # internal endpoint and enters the normal rotation path.
                inject_ps2_command(process, "audioinfo")
                if not wait_for_count(transcript, AUDIO_READY, 2, deadline):
                    detail = (
                        "audio service did not reintegrate with the "
                        "replacement driver")
            if detail:
                pass
            else:
                inject_ps2_command(process, "audioinfo")
            if not detail and not wait_for(transcript, AUDIOINFO_OK, deadline):
                detail = "audioinfo did not confirm the PCM service"
            if not detail:
                # More cycles than the service restart budget prove that
                # normal short-lived clients rotate their endpoint generation
                # without being misclassified as service failures.
                for cycle in range(1, AUDIO_TEST_CYCLES + 1):
                    inject_ps2_command(process, "audiotest")
                    if not wait_for_count(
                            transcript, AUDIOTEST_OK, cycle, deadline):
                        detail = f"audiotest cycle {cycle} did not complete"
                        break
                if (not detail and args.smp > 1 and not wait_for(
                        transcript, "REIST_AUDIO HDA_AP_EXEC cpu=", deadline)):
                    detail = (
                        "HDA driver did not execute an authorized operation "
                        "on an AP")
                if (not detail and (args.expect_audio_service_smp or
                                    args.expect_audio_service_smp_restart) and
                        not wait_for_count(
                            transcript, "REIST_AUDIO SERVICE_AP_EXEC cpu=",
                            AUDIO_TEST_CYCLES, deadline)):
                    detail = "not every rotated audio service executed on an AP"
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
