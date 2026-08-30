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
GCC_CLONE_SUFFIX_RE = re.compile(
    r'(?:\.(?:constprop|isra|part)\.\d+|\.cold)+$')
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
    "mount", "unmount", "open", "close", "read", "write", "sync",
    "readdir", "readdir_batch", "mkdir", "rmdir", "create", "delete",
    "rename", "truncate", "fstat", "touch", "write_chunk_capacity",
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

    budget_data: dict = {}
    if budget_file is not None:
        try:
            budget_data = json.loads(
                budget_file.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            errors.append(f"invalid stack budget file {budget_file}: {error}")

    indirect: dict[str, list[str]] = budget_data.get("indirect_calls", {})
    indirect_costs: dict[str, int] = budget_data.get("indirect_costs", {})
    external: dict[str, int] = budget_data.get("external_costs", {})

    def budget_lookup(mapping: dict, node: str):
        value = mapping.get(node)
        if value is not None:
            return value
        canonical = GCC_CLONE_SUFFIX_RE.sub("", node)
        return mapping.get(canonical) if canonical != node else None

    guarded_member_group: dict[str, str] = {}
    guarded_group_names: set[str] = set()
    guarded_groups = budget_data.get("guarded_reentry_groups", [])
    if not isinstance(guarded_groups, list):
        errors.append("guarded_reentry_groups must be a list")
        guarded_groups = []
    for group in guarded_groups:
        if not isinstance(group, dict):
            errors.append(f"invalid guarded reentry group: {group!r}")
            continue
        name = group.get("name")
        members = group.get("members")
        if (not isinstance(name, str) or not name or
                not isinstance(members, list) or not members or
                any(not isinstance(member, str) or not member
                    for member in members)):
            errors.append(f"invalid guarded reentry group: {group!r}")
            continue
        if name in guarded_group_names:
            errors.append(f"duplicate guarded reentry group: {name}")
            continue
        guarded_group_names.add(name)
        for member in members:
            previous = guarded_member_group.get(member)
            if previous is not None:
                errors.append(
                    f"guarded reentry member {member} belongs to "
                    f"both {previous} and {name}")
                continue
            guarded_member_group[member] = name
    for member, group in sorted(guarded_member_group.items()):
        if member not in stack_costs:
            errors.append(
                f"missing guarded reentry member {member} for {group}")

    def effective_children(node: str) -> set[str]:
        children = set(graph.get(node, ()))
        if "__indirect_call" not in children:
            return children
        children.remove("__indirect_call")
        targets = budget_lookup(indirect, node)
        fixed_cost = budget_lookup(indirect_costs, node)
        if targets:
            children.update(targets)
        elif isinstance(fixed_cost, int) and fixed_cost >= 0:
            pass
        else:
            children.add("__indirect_call")
        return children

    cycle_visiting: set[tuple[str, frozenset[str]]] = set()
    cycle_visited: set[tuple[str, frozenset[str]]] = set()
    cycle_path: list[tuple[str, frozenset[str]]] = []
    reported_cycles: set[tuple[tuple[str, tuple[str, ...]], ...]] = set()

    def visit_state(node: str, guards: frozenset[str]) -> None:
        group = guarded_member_group.get(node)
        if group is not None and group in guards:
            return
        entered_guards = guards if group is None else guards | {group}
        state = (node, entered_guards)
        if state in cycle_visited:
            return
        if state in cycle_visiting:
            cycle_start = cycle_path.index(state)
            states = cycle_path[cycle_start:] + [state]
            key = tuple((name, tuple(sorted(active_guards)))
                        for name, active_guards in states)
            if key not in reported_cycles:
                errors.append(
                    "recursive callgraph cycle is forbidden: " +
                    " -> ".join(name for name, _ in states))
                reported_cycles.add(key)
            return
        cycle_visiting.add(state)
        cycle_path.append(state)
        for target in sorted(effective_children(node)):
            visit_state(target, entered_guards)
        cycle_path.pop()
        cycle_visiting.remove(state)
        cycle_visited.add(state)

    for node in sorted(set(graph) | set(stack_costs)):
        visit_state(node, frozenset())

    budget_summaries: list[str] = []
    if budget_file is not None:
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
            ("FAT32 mutation hook", "fat32_mutation_hooks",
             re.compile(
                 r'fat32_context_mutation_hook\s*=\s*([A-Za-z_]\w*)'),
             {"NULL"}),
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
        budget_cycle_reported: set[tuple[str, frozenset[str]]] = set()
        unbound_indirect_reported: set[str] = set()
        path_memo: dict[
            tuple[str, frozenset[str]], tuple[int, tuple[str, ...]]
        ] = {}

        def path_cost(
                node: str,
                active: set[tuple[str, frozenset[str]]],
                guards: frozenset[str]) -> tuple[int, list[str], bool]:
            memo_key = (node, guards)
            cached = path_memo.get(memo_key)
            if cached is not None:
                return cached[0], list(cached[1]), True
            if node == "__indirect_call":
                if node not in unbound_indirect_reported:
                    errors.append("unbound indirect call in budgeted path")
                    unbound_indirect_reported.add(node)
                return 0, [node], True
            cost = stack_costs.get(node, external.get(node))
            if cost is None:
                if node not in missing_reported:
                    errors.append(f"missing stack cost in budgeted path: {node}")
                    missing_reported.add(node)
                cost = 0

            group = guarded_member_group.get(node)
            if group is not None and group in guards:
                path = [node, f"<guarded-terminal:{group}>"]
                path_memo[memo_key] = (cost, tuple(path))
                return cost, path, True
            entered_guards = guards if group is None else guards | {group}
            state = (node, entered_guards)
            if state in active:
                if state not in budget_cycle_reported:
                    errors.append(f"cycle in budgeted path: {node}")
                    budget_cycle_reported.add(state)
                return 0, [node], False

            active.add(state)
            children = set(graph.get(node, ()))
            maximum_child = (0, [])
            cycle_free = True
            if "__indirect_call" in children:
                children.remove("__indirect_call")
                targets = budget_lookup(indirect, node)
                fixed_cost = budget_lookup(indirect_costs, node)
                if targets:
                    children.update(targets)
                elif isinstance(fixed_cost, int) and fixed_cost >= 0:
                    maximum_child = (fixed_cost, ["<bounded-indirect>"])
                else:
                    if node not in unbound_indirect_reported:
                        errors.append(f"unbound indirect call from {node}")
                        unbound_indirect_reported.add(node)
            for child in sorted(children):
                candidate = path_cost(child, active, entered_guards)
                cycle_free = cycle_free and candidate[2]
                if candidate[0] > maximum_child[0]:
                    maximum_child = (candidate[0], candidate[1])
            active.remove(state)
            total = cost + maximum_child[0]
            path = [node] + maximum_child[1]
            if cycle_free:
                path_memo[memo_key] = (total, tuple(path))
            return total, path, cycle_free

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
            total, worst_path, _ = path_cost(
                root_name, set(), frozenset())
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
