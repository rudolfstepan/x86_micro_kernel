#!/usr/bin/env python3
"""Validate complete bounded compiler stack and callgraph evidence."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


NODE_RE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"')
EDGE_RE = re.compile(
    r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s+'
    r'targetname:\s*"([^"]+)"')


def validate(root: Path, expected: int, local_limit: int) -> tuple[list[str], int, str]:
    errors: list[str] = []
    root = root.resolve()
    usage_files = sorted(root.rglob("*.su"))
    graph_files = sorted(root.rglob("*.ci"))
    if len(usage_files) != expected:
        errors.append(f"expected {expected} stack files, found {len(usage_files)}")
    if len(graph_files) != expected:
        errors.append(f"expected {expected} callgraph files, found {len(graph_files)}")
    usage_stems = {path.relative_to(root).with_suffix("") for path in usage_files}
    graph_stems = {path.relative_to(root).with_suffix("") for path in graph_files}
    for stem in sorted(usage_stems ^ graph_stems):
        errors.append(f"unpaired stack/callgraph evidence: {stem}")

    maximum = 0
    maximum_name = ""
    function_count = 0
    for path in usage_files:
        for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1):
            fields = line.rsplit("\t", 2)
            if len(fields) != 3 or not fields[1].isdigit():
                errors.append(f"invalid stack record: {path}:{line_number}")
                continue
            name, size_text, kind = fields
            size = int(size_text)
            function_count += 1
            if kind != "static":
                errors.append(f"non-static stack bound {kind!r}: {name}")
            if size > local_limit:
                errors.append(f"local stack {size} exceeds {local_limit}: {name}")
            if size > maximum:
                maximum = size
                maximum_name = name

    node_count = 0
    edge_count = 0
    graph: dict[str, set[str]] = {}
    for path in graph_files:
        text = path.read_text(encoding="utf-8")
        nodes = NODE_RE.findall(text)
        edges = EDGE_RE.findall(text)
        node_count += len(nodes)
        edge_count += len(edges)
        for source, target in edges:
            graph.setdefault(source, set()).add(target)
            if source == target:
                errors.append(f"direct recursion is forbidden: {source}")

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(node: str, path: list[str]) -> None:
        if node in visited:
            return
        if node in visiting:
            cycle_start = path.index(node)
            cycle = " -> ".join(path[cycle_start:] + [node])
            errors.append(f"recursive callgraph cycle is forbidden: {cycle}")
            return
        visiting.add(node)
        path.append(node)
        for target in sorted(graph.get(node, ())):
            visit(target, path)
        path.pop()
        visiting.remove(node)
        visited.add(node)

    for node in sorted(graph):
        visit(node, [])

    if function_count == 0 or node_count == 0:
        errors.append("no function-level stack evidence")
    summary = (f"{len(usage_files)} objects, {function_count} stack records, "
               f"{node_count} graph nodes, {edge_count} edges, "
               f"max-local={maximum} ({maximum_name})")
    return errors, maximum, summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=int)
    parser.add_argument("--local-limit", required=True, type=int)
    args = parser.parse_args()
    errors, _, summary = validate(args.root, args.expected, args.local_limit)
    for error in errors:
        print(f"stack-evidence: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"stack-evidence: PASS ({summary})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
