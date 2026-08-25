#!/usr/bin/env python3
"""Generate the bounded 8x16 REIST BMP fallback from GNU Unifont HEX."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "assets/fonts/source/unifont-16.0.04.hex.gz"
DEFAULT_OUTPUT = ROOT / "assets/fonts/reist-unicode-bmp.psf"
SOURCE_SHA256 = "f9c8c7802453f47be02677176aeac2342ee96d354fad7a26cedcce48e68e1d9f"
PSF2_MAGIC = 0x864AB572
EXPECTED_GLYPHS = 57086
MAX_OUTPUT_BYTES = 2 * 1024 * 1024


def compress_wide_row(row: int) -> int:
    """Reduce one 16-pixel row to 8 pixels by OR-ing adjacent columns."""
    result = 0
    for target_x in range(8):
        if row & (0xC000 >> (target_x * 2)):
            result |= 0x80 >> target_x
    return result


def load_glyphs(source: Path) -> list[tuple[int, bytes]]:
    compressed = source.read_bytes()
    actual_hash = hashlib.sha256(compressed).hexdigest()
    if actual_hash != SOURCE_SHA256:
        raise RuntimeError(f"unexpected GNU Unifont source SHA-256 {actual_hash}")
    text = gzip.decompress(compressed).decode("ascii")
    glyphs: list[tuple[int, bytes]] = []
    previous = -1
    for line_number, line in enumerate(text.splitlines(), 1):
        try:
            scalar_text, bitmap_text = line.split(":", 1)
            scalar = int(scalar_text, 16)
            bitmap = bytes.fromhex(bitmap_text)
        except ValueError as error:
            raise RuntimeError(f"malformed Unifont line {line_number}") from error
        if scalar <= previous or scalar > 0xFFFF or 0xD800 <= scalar <= 0xDFFF:
            raise RuntimeError(f"invalid or unordered scalar on line {line_number}")
        if len(bitmap) == 16:
            raster = bitmap
        elif len(bitmap) == 32:
            raster = bytes(
                compress_wide_row(int.from_bytes(bitmap[row:row + 2], "big"))
                for row in range(0, 32, 2)
            )
        else:
            raise RuntimeError(f"unsupported glyph width on line {line_number}")
        glyphs.append((scalar, raster))
        previous = scalar
    if len(glyphs) != EXPECTED_GLYPHS:
        raise RuntimeError(
            f"expected {EXPECTED_GLYPHS} Unifont glyphs, found {len(glyphs)}")
    if not any(scalar == 0x25A0 for scalar, _ in glyphs):
        raise RuntimeError("fallback U+25A0 is absent")
    return glyphs


def generate(source: Path) -> bytes:
    glyphs = load_glyphs(source)
    rasters = b"".join(bitmap for _, bitmap in glyphs)
    unicode_table = b"".join(
        chr(scalar).encode("utf-8") + b"\xFF" for scalar, _ in glyphs)
    header = struct.pack(
        "<8I", PSF2_MAGIC, 0, 32, 1, len(glyphs), 16, 16, 8)
    output = header + rasters + unicode_table
    if len(output) >= MAX_OUTPUT_BYTES:
        raise RuntimeError("generated BMP font exceeds the 2 MiB contract")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    generated = generate(arguments.source)
    if arguments.check:
        if not arguments.output.is_file() or arguments.output.read_bytes() != generated:
            raise SystemExit("REIST GNU Unifont PSF2 asset is stale")
        return
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(generated)


if __name__ == "__main__":
    main()
