#!/usr/bin/env python3
"""Generate the bounded REIST GK208 GR tables from one pinned Linux tree."""

from __future__ import annotations

import argparse
import base64
import binascii
import re
import struct
import urllib.request
from pathlib import Path


COMMIT = "45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229"
BASE = (
    "https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/"
    f"linux/+/{COMMIT}/drivers/gpu/drm/nouveau/nvkm/engine/gr/"
)
SOURCES = (
    "gf100.c", "gf117.c", "gf119.c", "gk104.c", "gk110.c", "gk208.c",
    "ctxgf100.c", "ctxgf117.c", "ctxgk104.c", "ctxgk110.c", "ctxgk208.c",
)
MMIO_PACK = (
    "gk208_gr_init_main_0", "gk110_gr_init_fe_0",
    "gf100_gr_init_pri_0", "gf100_gr_init_rstr2d_0",
    "gf119_gr_init_pd_0", "gk208_gr_init_ds_0",
    "gf100_gr_init_scc_0", "gk110_gr_init_sked_0",
    "gk110_gr_init_cwd_0", "gf119_gr_init_prop_0",
    "gk208_gr_init_gpc_unk_0", "gf100_gr_init_setup_0",
    "gf100_gr_init_crstr_0", "gk208_gr_init_setup_1",
    "gf100_gr_init_zcull_0", "gf119_gr_init_gpm_0",
    "gk110_gr_init_gpc_unk_1", "gf100_gr_init_gcc_0",
    "gk104_gr_init_gpc_unk_2", "gk104_gr_init_tpccs_0",
    "gk208_gr_init_tex_0", "gk104_gr_init_pe_0",
    "gk208_gr_init_l1c_0", "gf100_gr_init_mpc_0",
    "gk110_gr_init_sm_0", "gf117_gr_init_pes_0",
    "gf117_gr_init_wwdx_0", "gf117_gr_init_cbm_0",
    "gk104_gr_init_be_0", "gf100_gr_init_fe_1",
)
CONTEXT_PACKS = (
    ("HUB", "gk208_grctx_pack_hub", 0x409000, 0x000, 0x000000),
    ("GPC0", "gk208_grctx_pack_gpc_0", 0x41A000, 0x000, 0x418000),
    ("GPC1", "gk208_grctx_pack_gpc_1", 0x41A000, 0x000, 0x418000),
    ("TPC", "gk208_grctx_pack_tpc", 0x41A000, 0x004, 0x419800),
    ("PPC", "gk208_grctx_pack_ppc", 0x41A000, 0x008, 0x41BE00),
)
GOLDEN_PACKS = (
    ("ICMD", "gk208_grctx_pack_icmd"),
    ("MTHD", "gk110_grctx_pack_mthd"),
)

INIT_RE = re.compile(
    r"(?:static\s+)?const\s+struct\s+gf100_gr_init\s+"
    r"([A-Za-z0-9_]+)\[\]\s*=\s*\{(.*?)\n\};", re.S)
PACK_RE = re.compile(
    r"(?:static\s+)?const\s+struct\s+gf100_gr_pack\s+"
    r"([A-Za-z0-9_]+)\[\]\s*=\s*\{(.*?)\n\};", re.S)
ROW_RE = re.compile(
    r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*([0-9]+)\s*,\s*"
    r"(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\}")
REF_RE = re.compile(r"\{\s*([A-Za-z0-9_]+)\s*\}")
TYPED_REF_RE = re.compile(
    r"\{\s*([A-Za-z0-9_]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\}")


def fetch(name: str) -> str:
    with urllib.request.urlopen(BASE + name + "?format=TEXT", timeout=30) as reply:
        return base64.b64decode(reply.read()).decode("utf-8")


def crc32(entries: list[tuple[int, int, int, int]]) -> int:
    payload = b"".join(struct.pack("<IIII", *entry) for entry in entries)
    return binascii.crc32(payload) & 0xFFFFFFFF


def crc32_method(entries: list[tuple[int, int, int, int, int]]) -> int:
    payload = b"".join(struct.pack("<IIIII", *entry) for entry in entries)
    return binascii.crc32(payload) & 0xFFFFFFFF


def parse_sources() -> tuple[dict[str, list[tuple[int, int, int, int]]],
                             dict[str, list[str]],
                             dict[str, list[tuple[str, int]]]]:
    arrays: dict[str, list[tuple[int, int, int, int]]] = {}
    packs: dict[str, list[str]] = {}
    typed_packs: dict[str, list[tuple[str, int]]] = {}
    for name in SOURCES:
        source = fetch(name)
        for match in INIT_RE.finditer(source):
            rows = [tuple(int(value, 0) for value in row)
                    for row in ROW_RE.findall(match.group(2))]
            if not rows:
                raise ValueError(f"empty or unparsable init array {match.group(1)}")
            if match.group(1) in arrays and arrays[match.group(1)] != rows:
                raise ValueError(f"conflicting init array {match.group(1)}")
            arrays[match.group(1)] = rows
        for match in PACK_RE.finditer(source):
            refs = REF_RE.findall(match.group(2))
            if refs:
                packs[match.group(1)] = refs
            typed_refs = [(name, int(kind, 0))
                          for name, kind in TYPED_REF_RE.findall(match.group(2))]
            if typed_refs:
                typed_packs[match.group(1)] = typed_refs
    return arrays, packs, typed_packs


def emit(output: Path) -> None:
    arrays, packs, typed_packs = parse_sources()
    actual_mmio = packs.get("gk208_gr_pack_mmio")
    if tuple(actual_mmio or ()) != MMIO_PACK:
        raise ValueError("pinned gk208_gr_pack_mmio order changed")

    mmio: list[tuple[int, int, int, int]] = []
    mmio_spans: list[tuple[int, int, str]] = []
    for name in MMIO_PACK:
        entries = arrays.get(name)
        if entries is None:
            raise ValueError(f"missing MMIO array {name}")
        mmio_spans.append((len(mmio), len(entries), name))
        mmio.extend(entries)

    context: list[tuple[int, int, int, int]] = []
    context_spans: list[tuple[int, int, int, int, int, str]] = []
    for label, pack_name, falcon, starstar, base in CONTEXT_PACKS:
        first = len(context)
        refs = packs.get(pack_name)
        if refs is None:
            raise ValueError(f"missing context pack {pack_name}")
        for name in refs:
            entries = arrays.get(name)
            if entries is None:
                raise ValueError(f"missing context array {name}")
            context.extend(entries)
        context_spans.append(
            (first, len(context) - first, falcon, starstar, base, label))

    golden: dict[str, list[tuple[int, int, int, int]]] = {}
    method: list[tuple[int, int, int, int, int]] = []
    for label, pack_name in GOLDEN_PACKS:
        if label == "MTHD":
            refs = typed_packs.get(pack_name)
            if refs is None:
                raise ValueError(f"missing typed golden-context pack {pack_name}")
            for name, class_id in refs:
                rows = arrays.get(name)
                if rows is None:
                    raise ValueError(f"missing golden-context array {name}")
                method.extend((*row, class_id) for row in rows)
            continue
        entries: list[tuple[int, int, int, int]] = []
        refs = packs.get(pack_name)
        if refs is None:
            raise ValueError(f"missing golden-context pack {pack_name}")
        for name in refs:
            rows = arrays.get(name)
            if rows is None:
                raise ValueError(f"missing golden-context array {name}")
            entries.extend(rows)
        golden[label] = entries

    lines = [
        "/* Generated by scripts/generate_nvidia_gk208_gr_tables.py.",
        f" * Linux/Nouveau commit: {COMMIT}",
        " * Source arrays are MIT licensed; see the generator and Linux files.",
        " * Do not edit manually.",
        " */",
        "#ifndef REIST_NVIDIA_GK208_GR_TABLES_H",
        "#define REIST_NVIDIA_GK208_GR_TABLES_H",
        "",
        f"#define REIST_GK208_GR_MMIO_TUPLE_COUNT {len(mmio)}U",
        f"#define REIST_GK208_GR_MMIO_PACK_COUNT {len(mmio_spans)}U",
        f"#define REIST_GK208_GR_MMIO_CRC32 0x{crc32(mmio):08X}U",
        f"#define REIST_GK208_GR_CONTEXT_TUPLE_COUNT {len(context)}U",
        f"#define REIST_GK208_GR_CONTEXT_PACK_COUNT {len(context_spans)}U",
        f"#define REIST_GK208_GR_CONTEXT_CRC32 0x{crc32(context):08X}U",
        f"#define REIST_GK208_GR_ICMD_TUPLE_COUNT {len(golden['ICMD'])}U",
        f"#define REIST_GK208_GR_ICMD_CRC32 0x{crc32(golden['ICMD']):08X}U",
        f"#define REIST_GK208_GR_MTHD_TUPLE_COUNT {len(method)}U",
        f"#define REIST_GK208_GR_MTHD_CRC32 0x{crc32_method(method):08X}U",
        "",
        "static const reist_nvidia_gk208_gr_tuple_t reist_gk208_gr_mmio[] = {",
    ]
    span_index = 0
    for index, entry in enumerate(mmio):
        if span_index < len(mmio_spans) and index == mmio_spans[span_index][0]:
            lines.append(f"    /* {mmio_spans[span_index][2]} */")
            span_index += 1
        lines.append("    { 0x%08XU, %dU, 0x%08XU, 0x%08XU }," % entry)
    lines.extend(("};", "", "static const reist_nvidia_gk208_gr_span_t "
                  "reist_gk208_gr_mmio_spans[] = {"))
    for first, count, name in mmio_spans:
        lines.append(f"    {{ {first}U, {count}U }}, /* {name} */")
    lines.extend(("};", "", "static const reist_nvidia_gk208_gr_tuple_t "
                  "reist_gk208_gr_context[] = {"))
    span_index = 0
    for index, entry in enumerate(context):
        if (span_index < len(context_spans) and
                index == context_spans[span_index][0]):
            lines.append(f"    /* {context_spans[span_index][5]} */")
            span_index += 1
        lines.append("    { 0x%08XU, %dU, 0x%08XU, 0x%08XU }," % entry)
    lines.extend(("};", "", "static const reist_nvidia_gk208_gr_context_span_t "
                  "reist_gk208_gr_context_spans[] = {"))
    for first, count, falcon, starstar, base, label in context_spans:
        lines.append(
            f"    {{ {first}U, {count}U, 0x{falcon:08X}U, "
            f"0x{starstar:08X}U, 0x{base:08X}U }}, /* {label} */")
    lines.extend(("};", ""))
    for label in ("ICMD",):
        lines.append("static const reist_nvidia_gk208_gr_tuple_t "
                     f"reist_gk208_gr_{label.lower()}[] = {{")
        for entry in golden[label]:
            lines.append("    { 0x%08XU, %dU, 0x%08XU, 0x%08XU }," % entry)
        lines.extend(("};", ""))
    lines.append("static const reist_nvidia_gk208_gr_method_tuple_t "
                 "reist_gk208_gr_mthd[] = {")
    for addr, count, pitch, value, class_id in method:
        lines.append("    { { 0x%08XU, %dU, 0x%08XU, 0x%08XU }, "
                     "0x%08XU }," %
                     (addr, count, pitch, value, class_id))
    lines.extend(("};", ""))
    lines.extend(("#endif", ""))
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    emit(args.output)


if __name__ == "__main__":
    main()
