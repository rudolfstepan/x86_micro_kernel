#!/usr/bin/env python3
"""Generate the deterministic REIST PSF2 fallback font."""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/video/framebuffer.c"
DEFAULT_OUTPUT = ROOT / "assets/fonts/reist-vga.psf"
MAGIC = 0x864AB572
EURO = bytes((
    0x00, 0x00, 0x1E, 0x21, 0x20, 0x7C, 0x20, 0x78,
    0x20, 0x20, 0x21, 0x1E, 0x00, 0x00, 0x00, 0x00,
))
CONTROL_GRAPHICS = {
    0x01: 0x263A, 0x02: 0x263B, 0x03: 0x2665, 0x04: 0x2666,
    0x05: 0x2663, 0x06: 0x2660, 0x07: 0x2022, 0x08: 0x25D8,
    0x09: 0x25CB, 0x0A: 0x25D9, 0x0B: 0x2642, 0x0C: 0x2640,
    0x0D: 0x266A, 0x0E: 0x266B, 0x0F: 0x263C, 0x10: 0x25BA,
    0x11: 0x25C4, 0x12: 0x2195, 0x13: 0x203C, 0x14: 0x00B6,
    0x15: 0x00A7, 0x16: 0x25AC, 0x17: 0x21A8, 0x18: 0x2191,
    0x19: 0x2193, 0x1A: 0x2192, 0x1B: 0x2190, 0x1C: 0x221F,
    0x1D: 0x2194, 0x1E: 0x25B2, 0x1F: 0x25BC, 0x7F: 0x2302,
}


def legacy_glyphs() -> bytes:
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index("static const uint8_t font_8x16[4096] = {")
    end = text.index("\n};", start)
    body = re.sub(r"/\*.*?\*/", "", text[start:end], flags=re.DOTALL)
    values = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    if len(values) != 4096:
        raise RuntimeError(f"expected 4096 VGA font bytes, found {len(values)}")
    return values


def aliases() -> list[list[int]]:
    result: list[set[int]] = [set() for _ in range(257)]
    for glyph in range(0x80):
        result[glyph].add(glyph)
    for glyph, scalar in CONTROL_GRAPHICS.items():
        result[glyph].add(scalar)
    for glyph in range(0x80, 0x100):
        result[glyph].add(ord(bytes((glyph,)).decode("cp437")))
    result[256].add(0x20AC)
    flattened = [sorted(values) for values in result]
    scalars = [value for values in flattened for value in values]
    if len(scalars) != len(set(scalars)):
        raise RuntimeError("PSF2 scalar aliases are ambiguous")
    return flattened


def generate() -> bytes:
    glyphs = legacy_glyphs() + EURO
    table = bytearray()
    for values in aliases():
        for scalar in values:
            table.extend(chr(scalar).encode("utf-8"))
        table.append(0xFF)
    header = struct.pack("<8I", MAGIC, 0, 32, 1, 257, 16, 16, 8)
    return header + glyphs + bytes(table)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated = generate()
    if args.check:
        if not args.output.is_file() or args.output.read_bytes() != generated:
            raise SystemExit("REIST PSF2 font is stale")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(generated)


if __name__ == "__main__":
    main()
