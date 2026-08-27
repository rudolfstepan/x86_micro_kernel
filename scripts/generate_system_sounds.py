"""Generate the original CC0 REIST classic system-sound set.

The output is deterministic mono 48-kHz S16_LE PCM.  No third-party or
Microsoft samples are used.
"""

from __future__ import annotations

import argparse
import math
import struct
import wave
from pathlib import Path


SAMPLE_RATE = 48_000
PEAK = 0.42


def segment(duration_ms: int, frequencies: tuple[float, ...], amplitude: float,
            attack_ms: int = 4, release_ms: int = 18) -> list[float]:
    frame_count = duration_ms * SAMPLE_RATE // 1000
    attack = max(1, attack_ms * SAMPLE_RATE // 1000)
    release = max(1, release_ms * SAMPLE_RATE // 1000)
    result: list[float] = []
    for frame in range(frame_count):
        envelope = min(1.0, frame / attack,
                       (frame_count - 1 - frame) / release)
        envelope = max(0.0, envelope)
        value = sum(math.sin(2.0 * math.pi * frequency * frame / SAMPLE_RATE)
                    for frequency in frequencies) / len(frequencies)
        result.append(value * amplitude * envelope)
    return result


def silence(duration_ms: int) -> list[float]:
    return [0.0] * (duration_ms * SAMPLE_RATE // 1000)


def normalize(samples: list[float]) -> bytes:
    maximum = max((abs(value) for value in samples), default=1.0)
    scale = PEAK / maximum if maximum > PEAK else 1.0
    encoded = bytearray()
    for value in samples:
        bounded = max(-1.0, min(1.0, value * scale))
        encoded.extend(struct.pack("<h", round(bounded * 32767.0)))
    return bytes(encoded)


def sound_set() -> dict[str, list[float]]:
    return {
        "startup.wav": (
            segment(70, (523.25, 659.25), 0.30) +
            segment(70, (659.25, 783.99), 0.34) +
            segment(120, (783.99, 1046.50), 0.38, release_ms=38)
        ),
        "shutdown.wav": (
            segment(80, (783.99, 1046.50), 0.34) +
            segment(80, (659.25, 783.99), 0.31) +
            segment(120, (392.00, 523.25), 0.36, release_ms=44)
        ),
        "error.wav": (
            segment(92, (220.00, 277.18), 0.40, attack_ms=2,
                    release_ms=24) +
            silence(22) +
            segment(120, (185.00, 233.08), 0.42, attack_ms=2,
                    release_ms=42)
        ),
        "notify.wav": (
            segment(72, (659.25, 987.77), 0.30) +
            silence(12) +
            segment(110, (783.99, 1174.66), 0.35, release_ms=38)
        ),
        "trash-drop.wav": (
            segment(54, (392.00, 523.25), 0.28, attack_ms=2,
                    release_ms=16) +
            segment(86, (196.00, 261.63), 0.38, attack_ms=1,
                    release_ms=42)
        ),
        "trash-empty.wav": (
            segment(58, (329.63, 493.88), 0.31, attack_ms=2,
                    release_ms=14) +
            silence(10) +
            segment(66, (246.94, 369.99), 0.34, attack_ms=1,
                    release_ms=18) +
            segment(96, (123.47, 185.00), 0.40, attack_ms=1,
                    release_ms=46)
        ),
    }


def write_wave(path: Path, samples: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.setcomptype("NONE", "not compressed")
        output.writeframes(normalize(samples))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir", type=Path,
        default=Path(__file__).resolve().parents[1] / "assets" / "audio",
    )
    args = parser.parse_args()
    for name, samples in sound_set().items():
        write_wave(args.output_dir / name, samples)


if __name__ == "__main__":
    main()
