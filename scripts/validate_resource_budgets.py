#!/usr/bin/env python3
"""Validate REIST resource budgets against their C preprocessor constants."""

from __future__ import annotations

import argparse
import ast
import operator
from pathlib import Path
import re
import sys
import tomllib

ID_PATTERN = re.compile(r"^RB-[A-Z0-9]+(?:-[A-Z0-9]+)*$")
SYMBOL_PATTERN = re.compile(r"^[A-Z][A-Z0-9_]*$")
INTEGER_SUFFIX = re.compile(r"(?i)\b(0x[0-9a-f]+|[0-9]+)[ul]+\b")
MAX_BUDGET = (1 << 64) - 1

BIN_OPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.floordiv,
    ast.FloorDiv: operator.floordiv,
    ast.Mod: operator.mod,
    ast.LShift: operator.lshift,
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_,
    ast.BitXor: operator.xor,
}
UNARY_OPS = {
    ast.UAdd: operator.pos,
    ast.USub: operator.neg,
    ast.Invert: operator.invert,
}


def _repository_file(root: Path, value: object, prefix: str,
                     errors: list[str]) -> Path | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{prefix} must be a non-empty repository path")
        return None
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        errors.append(f"{prefix} contains an unsafe path")
        return None
    resolved = (root / path).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        errors.append(f"{prefix} escapes repository")
        return None
    if not resolved.is_file():
        errors.append(f"{prefix} target does not exist: {value}")
        return None
    return resolved


def _integer_expression(node: ast.AST) -> int:
    if isinstance(node, ast.Expression):
        return _integer_expression(node.body)
    if isinstance(node, ast.Constant) and type(node.value) is int:
        if not 0 <= node.value <= MAX_BUDGET:
            raise ValueError("integer exceeds uint64 evaluation bounds")
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in BIN_OPS:
        left = _integer_expression(node.left)
        right = _integer_expression(node.right)
        if isinstance(node.op, (ast.Div, ast.FloorDiv, ast.Mod)) and right == 0:
            raise ValueError("division by zero")
        if isinstance(node.op, (ast.LShift, ast.RShift)) and right > 63:
            raise ValueError("shift exceeds uint64 evaluation bounds")
        result = BIN_OPS[type(node.op)](left, right)
        if not 0 <= result <= MAX_BUDGET:
            raise ValueError("result exceeds uint64 evaluation bounds")
        return result
    if isinstance(node, ast.UnaryOp) and type(node.op) in UNARY_OPS:
        result = UNARY_OPS[type(node.op)](_integer_expression(node.operand))
        if not 0 <= result <= MAX_BUDGET:
            raise ValueError("result exceeds uint64 evaluation bounds")
        return result
    raise ValueError("unsupported token in integer expression")


def _read_macro(source: Path, symbol: str) -> int:
    text = source.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^[ \t]*#[ \t]*define[ \t]+{re.escape(symbol)}[ \t]+(.+?)\s*$",
        re.MULTILINE,
    )
    matches = pattern.findall(text)
    if len(matches) != 1:
        raise ValueError(f"expected exactly one object macro {symbol}")
    expression = re.sub(r"/\*.*?\*/", "", matches[0])
    expression = expression.split("//", 1)[0].strip()
    expression = INTEGER_SUFFIX.sub(r"\1", expression)
    try:
        tree = ast.parse(expression, mode="eval")
        value = _integer_expression(tree)
    except (SyntaxError, ValueError, OverflowError) as error:
        raise ValueError(f"unsupported value for {symbol}: {error}") from error
    if value <= 0 or value > MAX_BUDGET:
        raise ValueError(f"{symbol} is outside the positive uint64 range")
    return value


def validate(register: Path, root: Path) -> list[str]:
    errors: list[str] = []
    try:
        document = tomllib.loads(register.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        return [f"cannot read resource budget register: {error}"]
    if document.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    if document.get("status") not in {"partial", "complete"}:
        errors.append("status must be partial or complete")
    budgets = document.get("budget")
    if not isinstance(budgets, list) or not budgets:
        return errors + ["at least one [[budget]] is required"]

    seen_ids: set[str] = set()
    seen_symbols: set[tuple[str, str]] = set()
    for index, budget in enumerate(budgets, 1):
        prefix = f"budget[{index}]"
        if not isinstance(budget, dict):
            errors.append(f"{prefix} must be a table")
            continue
        budget_id = budget.get("id")
        if not isinstance(budget_id, str) or not ID_PATTERN.fullmatch(budget_id):
            errors.append(f"{prefix}.id has invalid format")
        elif budget_id in seen_ids:
            errors.append(f"{prefix}.id is duplicated: {budget_id}")
        else:
            seen_ids.add(budget_id)
        for field in ("description", "unit"):
            if not isinstance(budget.get(field), str) or not budget[field].strip():
                errors.append(f"{prefix}.{field} must be non-empty")
        limit = budget.get("limit")
        if not isinstance(limit, int) or isinstance(limit, bool) or not 0 < limit <= MAX_BUDGET:
            errors.append(f"{prefix}.limit must be a positive uint64 integer")

        symbol = budget.get("symbol")
        if not isinstance(symbol, str) or not SYMBOL_PATTERN.fullmatch(symbol):
            errors.append(f"{prefix}.symbol has invalid format")
            symbol = None
        source_value = budget.get("source")
        source = _repository_file(root, source_value, f"{prefix}.source", errors)
        if source is not None and symbol is not None:
            key = (str(source.resolve()), symbol)
            if key in seen_symbols:
                errors.append(f"{prefix}.symbol is registered more than once")
            else:
                seen_symbols.add(key)
            try:
                actual = _read_macro(source, symbol)
                if isinstance(limit, int) and not isinstance(limit, bool) and actual != limit:
                    errors.append(
                        f"{prefix}.limit drift: {symbol} is {actual}, register says {limit}")
            except (OSError, ValueError) as error:
                errors.append(f"{prefix}.source cannot verify {symbol}: {error}")

        verification = budget.get("verification")
        if not isinstance(verification, list) or not verification:
            errors.append(f"{prefix}.verification must not be empty")
        else:
            for value in verification:
                if not isinstance(value, str) or not value.startswith("test/"):
                    errors.append(f"{prefix}.verification must reference test/")
                    continue
                _repository_file(root, value, f"{prefix}.verification", errors)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("register", nargs="?", default="safety/resource_budgets.toml")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    errors = validate((root / args.register).resolve(), root)
    for error in errors:
        print(f"resource-budgets: {error}", file=sys.stderr)
    if errors:
        return 1
    print("resource-budgets: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
