#!/usr/bin/env python3
"""Generate the deterministic REIST PSF2 system and editor fonts."""

from __future__ import annotations

import argparse
import hashlib
import math
import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/video/framebuffer.c"
DEFAULT_OUTPUT = ROOT / "assets/fonts/reist-vga.psf"
MAGIC = 0x864AB572
EDITOR_PILLOW_VERSION = "12.1.0"
EDITOR_HEIGHTS = (10, 12, 14, 16, 18, 20, 24, 28)
EDITOR_MIN_POINT_SIZE = 4
EDITOR_MAX_POINT_SIZE = 32
EDITOR_GRAYSCALE_THRESHOLD = 96
EDITOR_SCALARS = tuple(range(0x20, 0x7F)) + (0x25A0,)
EDITOR_FONTS = (
    ("jetbrains-mono",
     ROOT / "assets/fonts/source/jetbrains-mono/JetBrainsMono-Regular.ttf",
     ROOT / "assets/fonts/reist-jetbrains-mono.psf"),
    ("source-code-pro",
     ROOT / "assets/fonts/source/source-code-pro/SourceCodePro-Regular.otf",
     ROOT / "assets/fonts/reist-source-code-pro.psf"),
    ("iosevka",
     ROOT / "assets/fonts/source/iosevka/Iosevka-Regular.ttf",
     ROOT / "assets/fonts/reist-iosevka.psf"),
    ("fira-code",
     ROOT / "assets/fonts/source/fira-code/FiraCode-Regular.ttf",
     ROOT / "assets/fonts/reist-fira-code.psf"),
)
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


def generate_vga() -> bytes:
    glyphs = legacy_glyphs() + EURO
    table = bytearray()
    for values in aliases():
        for scalar in values:
            table.extend(chr(scalar).encode("utf-8"))
        table.append(0xFF)
    header = struct.pack("<8I", MAGIC, 0, 32, 1, 257, 16, 16, 8)
    return header + glyphs + bytes(table)


def editor_font_for_height(source: Path, target_height: int):
    """Choose the largest hinted face whose ASCII box fits the exact cell."""
    try:
        from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version
    except ImportError as error:
        raise RuntimeError("Pillow is required for editor-font generation") from error
    if pillow_version != EDITOR_PILLOW_VERSION:
        raise RuntimeError(
            f"Pillow {EDITOR_PILLOW_VERSION} is required, found {pillow_version}")

    selected = None
    for point_size in range(EDITOR_MIN_POINT_SIZE,
                            EDITOR_MAX_POINT_SIZE + 1):
        font = ImageFont.truetype(
            str(source), point_size, layout_engine=ImageFont.Layout.BASIC)
        printable = [float(font.getlength(chr(scalar)))
                     for scalar in EDITOR_SCALARS[:-1]]
        if max(printable) - min(printable) > 0.01:
            raise RuntimeError(f"{source.name} is not monospaced")
        cell_width = max(1, math.ceil(max(printable)))
        probe_height = 96
        probe_baseline = probe_height // 2
        top = probe_height
        bottom = 0
        for scalar in EDITOR_SCALARS[1:-1]:
            character = chr(scalar)
            box = font.getbbox(character, anchor="ls")
            glyph_width = box[2] - box[0]
            left = (cell_width - glyph_width) // 2 - box[0]
            probe = Image.new("L", (cell_width, probe_height), 0)
            ImageDraw.Draw(probe).text(
                (left, probe_baseline), character, font=font, fill=255,
                anchor="ls")
            thresholded = probe.point(
                lambda value: 255
                if value >= EDITOR_GRAYSCALE_THRESHOLD else 0)
            bounds = thresholded.getbbox()
            if bounds is not None:
                top = min(top, bounds[1] - probe_baseline)
                bottom = max(bottom, bounds[3] - probe_baseline)
        if cell_width <= 32 and bottom - top <= target_height:
            selected = (font, cell_width, top, bottom, point_size)
    if selected is None:
        raise RuntimeError(
            f"{source.name} has no face fitting {target_height}px")
    return selected


def generate_editor(source: Path, target_height: int) -> tuple[bytes, int, int]:
    """Rasterize one exact hinted size into a bounded standard PSF2 subset."""
    from PIL import Image, ImageDraw

    font, cell_width, top, bottom, point_size = editor_font_for_height(
        source, target_height)
    baseline = (target_height - (bottom - top)) // 2 - top
    row_bytes = (cell_width + 7) // 8
    glyphs = bytearray()
    table = bytearray()
    for scalar in EDITOR_SCALARS:
        canvas = Image.new("L", (cell_width, target_height), 0)
        draw = ImageDraw.Draw(canvas)
        if scalar == 0x25A0:
            inset = max(1, min(cell_width, target_height) // 5)
            draw.rectangle((inset, (target_height - cell_width) // 2 + inset,
                            cell_width - inset - 1,
                            (target_height + cell_width) // 2 - inset - 1),
                           fill=255)
        else:
            box = font.getbbox(chr(scalar), anchor="ls")
            glyph_width = box[2] - box[0]
            left = (cell_width - glyph_width) // 2 - box[0]
            draw.text((left, baseline), chr(scalar), font=font, fill=255,
                      anchor="ls")
        for y in range(target_height):
            for byte_index in range(row_bytes):
                value = 0
                for bit in range(8):
                    x = byte_index * 8 + bit
                    if (x < cell_width and canvas.getpixel((x, y)) >=
                            EDITOR_GRAYSCALE_THRESHOLD):
                        value |= 0x80 >> bit
                glyphs.append(value)
        table.extend(chr(scalar).encode("utf-8"))
        table.append(0xFF)
    header = struct.pack(
        "<8I", MAGIC, 0, 32, 1, len(EDITOR_SCALARS),
        row_bytes * target_height, target_height, cell_width)
    return header + bytes(glyphs) + bytes(table), cell_width, point_size


def editor_output(base: Path, target_height: int) -> Path:
    if target_height == 24:
        return base
    return base.with_name(f"{base.stem}-{target_height}{base.suffix}")


def generated_assets() -> list[tuple[Path, bytes]]:
    assets = [(DEFAULT_OUTPUT, generate_vga())]
    for _name, source, output in EDITOR_FONTS:
        if not source.is_file():
            raise RuntimeError(f"missing pinned editor font: {source}")
        for target_height in EDITOR_HEIGHTS:
            generated, _width, _point_size = generate_editor(
                source, target_height)
            assets.append((editor_output(output, target_height), generated))
    editor_hashes = [hashlib.sha256(data).digest() for _, data in assets[1:]]
    if len(editor_hashes) != len(set(editor_hashes)):
        raise RuntimeError("editor PSF2 outputs are not distinct")
    return assets


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.output is not None:
        assets = [(args.output, generate_vga())]
    else:
        assets = generated_assets()
    if args.check:
        stale = [str(path.relative_to(ROOT)) for path, data in assets
                 if not path.is_file() or path.read_bytes() != data]
        if stale:
            raise SystemExit("REIST PSF2 font is stale: " + ", ".join(stale))
        return
    for output, generated in assets:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(generated)


if __name__ == "__main__":
    main()
