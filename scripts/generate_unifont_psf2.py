#!/usr/bin/env python3
"""Generate the bounded 8x16 REIST Unicode fallback from GNU Unifont HEX."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "assets/fonts/source/unifont_all-16.0.04.hex.gz"
DEFAULT_OUTPUT = ROOT / "assets/fonts/reist-unicode.psf"
SOURCE_SHA256 = "20e8b505f602488697979eefc69857f7f6106bceab702f5ac559f4f84e0e7494"
PSF2_MAGIC = 0x864AB572
EXPECTED_GLYPHS = 126086
EXPECTED_BMP_GLYPHS = 60518
EXPECTED_SUPPLEMENTARY_GLYPHS = 65568
MAX_OUTPUT_BYTES = 3 * 1024 * 1024


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
        if (scalar <= previous or scalar > 0x10FFFF or
                0xD800 <= scalar <= 0xDFFF):
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
    bmp_count = sum(scalar <= 0xFFFF for scalar, _ in glyphs)
    supplementary_count = len(glyphs) - bmp_count
    if (bmp_count != EXPECTED_BMP_GLYPHS or
            supplementary_count != EXPECTED_SUPPLEMENTARY_GLYPHS):
        raise RuntimeError(
            "unexpected BMP/supplementary split "
            f"{bmp_count}/{supplementary_count}")
    if not any(scalar == 0x25A0 for scalar, _ in glyphs):
        raise RuntimeError("fallback U+25A0 is absent")
    for scalar in (0x10348, 0x1D11E, 0x1F600, 0x1F680, 0x20000):
        if not any(candidate == scalar for candidate, _ in glyphs):
            raise RuntimeError(f"required supplementary sample U+{scalar:X} is absent")
    if any(scalar == 0x10FFFD for scalar, _ in glyphs):
        raise RuntimeError("fallback probe scalar U+10FFFD unexpectedly mapped")
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
        raise RuntimeError("generated Unicode font exceeds the 3 MiB contract")
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
            raise SystemExit("REIST GNU Unifont all-plane PSF2 asset is stale")
        return
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(generated)


if __name__ == "__main__":
    main()
