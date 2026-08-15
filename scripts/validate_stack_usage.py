#!/usr/bin/env python3
"""Validate complete bounded compiler stack and callgraph evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


NODE_RE = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"')
EDGE_RE = re.compile(
    r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s+'
    r'targetname:\s*"([^"]+)"')
NODE_SIZE_RE = re.compile(r'\\n(\d+) bytes \(static\)"')
IRQ_REGISTRATION_RE = re.compile(
    r'register_interrupt_handler\s*\(\s*[^,]+,\s*'
    r'(?:\(\s*void\s*\*\s*\)\s*)?([A-Za-z_]\w*)')
EXCEPTION_HANDLER_RE = re.compile(
    r'exception_handlers\s*\[[^\]]+\]\s*=\s*([A-Za-z_]\w*)')
FAT32_SYNC_HOOK_RE = re.compile(
    r'fat32_context_sync_hook\s*=\s*([A-Za-z_]\w*)')
EXT2_DIR_VISITOR_RE = re.compile(
    r'ext2_walk_dir\s*\(\s*[^,]+,\s*[^,]+,\s*([A-Za-z_]\w*)')
VFS_BUDGETED_OPERATIONS = (
    "open", "close", "read", "write", "sync", "readdir",
    "readdir_batch", "mkdir", "rmdir", "create", "delete", "rename",
    "stat", "space",
)


def validate(root: Path, expected: int, local_limit: int,
             budget_file: Path | None = None,
             source_root: Path | None = None) -> tuple[list[str], int, str]:
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
    stack_costs: dict[str, int] = {}
    for path in graph_files:
        text = path.read_text(encoding="utf-8")
        nodes = NODE_RE.findall(text)
        edges = EDGE_RE.findall(text)
        node_count += len(nodes)
        edge_count += len(edges)
        for line_number, line in enumerate(text.splitlines(), 1):
            node_match = NODE_RE.search(line)
            size_match = NODE_SIZE_RE.search(line)
            if node_match is None or size_match is None:
                continue
            name = node_match.group(1)
            size = int(size_match.group(1))
            previous = stack_costs.get(name)
            if previous is not None and previous != size:
                errors.append(
                    f"conflicting stack costs for {name}: {previous}/{size} "
                    f"at {path}:{line_number}")
            stack_costs[name] = size
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

    budget_summaries: list[str] = []
    if budget_file is not None:
        try:
            budget_data = json.loads(
                budget_file.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            errors.append(f"invalid stack budget file {budget_file}: {error}")
            budget_data = {}

        indirect: dict[str, list[str]] = budget_data.get("indirect_calls", {})
        indirect_costs: dict[str, int] = budget_data.get("indirect_costs", {})
        external: dict[str, int] = budget_data.get("external_costs", {})
        entry_budgets = budget_data.get("entry_budgets", [])
        if not entry_budgets:
            errors.append("stack budget file contains no entry budgets")

        inventories = (
            ("registered IRQ handler", "registered_irq_handlers",
             IRQ_REGISTRATION_RE, {"void"}),
            ("CPU exception handler", "exception_handlers",
             EXCEPTION_HANDLER_RE, set()),
            ("FAT32 sync hook", "fat32_sync_hooks",
             FAT32_SYNC_HOOK_RE, {"NULL"}),
            ("EXT2 directory visitor", "ext2_dir_visitors",
             EXT2_DIR_VISITOR_RE, {"visitor", "ext2_dir_visitor_t"}),
        ) + tuple(
            (f"VFS {operation} handler", f"vfs_{operation}_handlers",
             re.compile(rf'\.{operation}\s*=\s*([A-Za-z_]\w*)'), {"NULL"})
            for operation in VFS_BUDGETED_OPERATIONS
        )
        requested_inventories = [
            item for item in inventories if budget_data.get(item[1])]
        if requested_inventories and source_root is None:
            errors.append("source inventories require --source-root")
        elif requested_inventories:
            resolved_source_root = source_root.resolve()
            production_roots = [
                resolved_source_root / name
                for name in ("arch", "drivers", "fs", "kernel", "lib", "mm")
                if (resolved_source_root / name).is_dir()]
            source_files = (
                [source for directory in production_roots
                 for source in directory.rglob("*.c")]
                if production_roots else list(resolved_source_root.rglob("*.c")))
            source_text = "\n".join(
                source.read_text(encoding="utf-8")
                for source in source_files)
            vfs_adapter_text = "\n".join(
                source.read_text(encoding="utf-8")
                for source in source_files
                if source.name.endswith("_vfs_adapter.c"))
            for label, key, pattern, ignored in requested_inventories:
                expected = set(budget_data[key])
                inventory_text = (
                    vfs_adapter_text if key.startswith("vfs_") else source_text)
                actual = set(pattern.findall(inventory_text)) - ignored
                for name in sorted(actual - expected):
                    errors.append(f"unbudgeted {label}: {name}")
                for name in sorted(expected - actual):
                    errors.append(f"stale {label} budget: {name}")

        missing_reported: set[str] = set()

        def path_cost(node: str, active: set[str]) -> tuple[int, list[str]]:
            if node in active:
                return 0, [node]
            if node == "__indirect_call":
                errors.append("unbound indirect call in budgeted path")
                return 0, [node]
            cost = stack_costs.get(node, external.get(node))
            if cost is None:
                if node not in missing_reported:
                    errors.append(f"missing stack cost in budgeted path: {node}")
                    missing_reported.add(node)
                cost = 0
            active.add(node)
            children = set(graph.get(node, ()))
            maximum_child = (0, [])
            if "__indirect_call" in children:
                children.remove("__indirect_call")
                targets = indirect.get(node)
                fixed_cost = indirect_costs.get(node)
                if targets:
                    children.update(targets)
                elif isinstance(fixed_cost, int) and fixed_cost >= 0:
                    maximum_child = (fixed_cost, ["<bounded-indirect>"])
                else:
                    errors.append(f"unbound indirect call from {node}")
            for child in sorted(children):
                candidate = path_cost(child, active)
                if candidate[0] > maximum_child[0]:
                    maximum_child = candidate
            active.remove(node)
            return cost + maximum_child[0], [node] + maximum_child[1]

        for entry in entry_budgets:
            name = entry.get("name", "")
            root_name = entry.get("root", "")
            limit = entry.get("limit", 0)
            entry_reserve = entry.get("entry_reserve", 0)
            if (not name or not root_name or not isinstance(limit, int) or
                    limit <= 0 or not isinstance(entry_reserve, int) or
                    entry_reserve < 0):
                errors.append(f"invalid entry budget: {entry!r}")
                continue
            total, worst_path = path_cost(root_name, set())
            total += entry_reserve
            if entry_reserve:
                worst_path.insert(0, f"<entry-reserve:{entry_reserve}>")
            if total > limit:
                errors.append(
                    f"entry stack {name} uses {total}, exceeds {limit}: "
                    + " -> ".join(worst_path))
            budget_summaries.append(f"{name}={total}/{limit}")

    if function_count == 0 or node_count == 0:
        errors.append("no function-level stack evidence")
    summary = (f"{len(usage_files)} objects, {function_count} stack records, "
               f"{node_count} graph nodes, {edge_count} edges, "
               f"max-local={maximum} ({maximum_name})")
    if budget_summaries:
        summary += ", entry-budgets=" + ",".join(budget_summaries)
    return errors, maximum, summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--expected", required=True, type=int)
    parser.add_argument("--local-limit", required=True, type=int)
    parser.add_argument("--budget-file", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    errors, _, summary = validate(
        args.root, args.expected, args.local_limit, args.budget_file,
        args.source_root)
    for error in errors:
        print(f"stack-evidence: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"stack-evidence: PASS ({summary})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
