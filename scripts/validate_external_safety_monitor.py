#!/usr/bin/env python3
"""Validate the fail-closed external watchdog/interlock acceptance profile."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import sys
import tomllib
from typing import Any

STATUSES = {"unbound", "selected", "qualified"}
MESSAGE_TYPES = {"ARM", "HEARTBEAT", "FENCE", "STATUS", "RESET"}
SCENARIOS = {
    "heartbeat-loss",
    "target-cpu-stuck",
    "transport-loss",
    "corrupt-frame",
    "replayed-frame",
    "fence-driver-fault",
    "readback-disagreement",
    "target-reset-failure",
    "monitor-power-domain-loss",
}
DISALLOWED_EVIDENCE = {"source-pattern", "host-model", "emulator-only"}
SHA256 = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER = re.compile(r"^[A-Z0-9][A-Z0-9._-]*$")


def _load(path: Path, errors: list[str]) -> dict[str, Any] | None:
    try:
        document = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        errors.append(f"cannot read profile: {error}")
        return None
    if not isinstance(document, dict):
        errors.append("profile must be a TOML document")
        return None
    return document


def _table(document: dict[str, Any], name: str,
           errors: list[str]) -> dict[str, Any]:
    value = document.get(name)
    if not isinstance(value, dict):
        errors.append(f"{name} must be a table")
        return {}
    return value


def _text(table: dict[str, Any], field: str, prefix: str,
          errors: list[str], *, empty: bool = False) -> str:
    value = table.get(field)
    if not isinstance(value, str) or (not empty and not value.strip()):
        errors.append(f"{prefix}.{field} must be " +
                      ("text" if empty else "non-empty text"))
        return ""
    return value


def _flag(table: dict[str, Any], field: str, prefix: str,
          errors: list[str]) -> bool | None:
    value = table.get(field)
    if not isinstance(value, bool):
        errors.append(f"{prefix}.{field} must be boolean")
        return None
    return value


def _positive(table: dict[str, Any], field: str, prefix: str,
              errors: list[str]) -> int:
    value = table.get(field)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        errors.append(f"{prefix}.{field} must be a positive integer")
        return 0
    return value


def _string_set(table: dict[str, Any], field: str, prefix: str,
                errors: list[str]) -> set[str]:
    value = table.get(field)
    if (not isinstance(value, list) or
            any(not isinstance(item, str) or not item for item in value)):
        errors.append(f"{prefix}.{field} must be a text list")
        return set()
    if len(value) != len(set(value)):
        errors.append(f"{prefix}.{field} contains duplicates")
    return set(value)


def _evidence_file(root: Path, relative: object, digest: object,
                   prefix: str, errors: list[str]) -> None:
    if (not isinstance(relative, str) or not relative or
            Path(relative).is_absolute() or ".." in Path(relative).parts):
        errors.append(f"{prefix} contains an unsafe path")
        return
    path = (root / relative).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError:
        errors.append(f"{prefix} escapes repository")
        return
    if not path.is_file():
        errors.append(f"{prefix} does not exist: {relative}")
        return
    if not isinstance(digest, str) or not SHA256.fullmatch(digest):
        errors.append(f"{prefix}_sha256 must be lowercase SHA-256")
        return
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != digest:
        errors.append(f"{prefix} SHA-256 mismatch: {relative}")


def _validate_trace_links(document: dict[str, Any], root: Path,
                          ftti_ms: int, errors: list[str]) -> None:
    hazard_path = root / "safety/hazards.toml"
    scope_path = root / "safety/assurance_scope.toml"
    try:
        hazards = tomllib.loads(hazard_path.read_text(encoding="utf-8"))
        scope = tomllib.loads(scope_path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        errors.append(f"cannot read linked assurance data: {error}")
        return
    hazard_id = document.get("hazard_id")
    linked = [item for item in hazards.get("hazard", [])
              if isinstance(item, dict) and item.get("id") == hazard_id]
    if len(linked) != 1:
        errors.append("hazard_id must resolve exactly once in hazards.toml")
    else:
        if linked[0].get("ftti_ms") != ftti_ms:
            errors.append("timing.ftti_ms does not match linked hazard")
        if document.get("requirement_id") not in linked[0].get("requirements", []):
            errors.append("linked hazard does not reference requirement_id")
    requirement_id = document.get("requirement_id")
    requirements = [item for item in scope.get("requirement", [])
                    if isinstance(item, dict) and item.get("id") == requirement_id]
    if len(requirements) != 1:
        errors.append("requirement_id must resolve exactly once in assurance scope")


def validate(profile_path: Path, root: Path) -> list[str]:
    errors: list[str] = []
    document = _load(profile_path, errors)
    if document is None:
        return errors
    if document.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    profile_id = document.get("profile_id")
    if not isinstance(profile_id, str) or not IDENTIFIER.fullmatch(profile_id):
        errors.append("profile_id has invalid format")
    status = document.get("status")
    if status not in STATUSES:
        errors.append("status must be unbound, selected or qualified")
    if not isinstance(document.get("hazard_id"), str) or not re.fullmatch(
            r"HZ-[A-Z0-9]+-[0-9]{3}", document.get("hazard_id", "")):
        errors.append("hazard_id has invalid format")
    if not isinstance(document.get("requirement_id"), str) or not re.fullmatch(
            r"REQ-[A-Z0-9-]+-[0-9]{3}", document.get("requirement_id", "")):
        errors.append("requirement_id has invalid format")
    _text(document, "claim", "profile", errors)

    target = _table(document, "target", errors)
    monitor = _table(document, "monitor", errors)
    independence = _table(document, "independence", errors)
    timing = _table(document, "timing", errors)
    protocol = _table(document, "protocol", errors)
    fence = _table(document, "fence", errors)
    failure = _table(document, "failure_policy", errors)
    qualification = _table(document, "qualification", errors)

    target_selected = _flag(target, "selected", "target", errors)
    monitor_selected = _flag(monitor, "selected", "monitor", errors)
    target_fields = ["system_id", "board_model", "board_revision",
                     "serial_number"]
    monitor_fields = ["manufacturer", "model", "hardware_revision",
                      "firmware_version", "transport"]
    target_values = [_text(target, field, "target", errors, empty=True)
                     for field in target_fields]
    monitor_values = [_text(monitor, field, "monitor", errors, empty=True)
                      for field in monitor_fields]
    firmware_sha = _text(monitor, "firmware_sha256", "monitor", errors,
                         empty=True)

    independence_fields = [
        "separate_power_domain", "separate_clock_domain",
        "target_cpu_reset_independent", "hazard_output_driver_independent",
        "power_loss_defaults_safe",
    ]
    independence_values = {
        field: _flag(independence, field, "independence", errors)
        for field in independence_fields
    }

    deadline_fields = [
        "ftti_ms", "heartbeat_period_ms", "heartbeat_loss_deadline_ms",
        "fence_apply_deadline_ms", "fence_readback_deadline_ms",
        "reset_assert_deadline_ms",
    ]
    deadlines = {field: _positive(timing, field, "timing", errors)
                 for field in deadline_fields}
    allocated = sum(deadlines[field] for field in (
        "heartbeat_loss_deadline_ms", "fence_apply_deadline_ms",
        "fence_readback_deadline_ms", "reset_assert_deadline_ms"))
    if allocated > deadlines["ftti_ms"]:
        errors.append("timing deadline allocation exceeds ftti_ms")
    if deadlines["heartbeat_period_ms"] >= deadlines["heartbeat_loss_deadline_ms"]:
        errors.append("heartbeat_period_ms must be below loss deadline")
    _validate_trace_links(document, root, deadlines["ftti_ms"], errors)

    if protocol.get("name") != "reist.external-monitor/1":
        errors.append("protocol.name must be reist.external-monitor/1")
    if protocol.get("magic") != 0x524D4F4E:
        errors.append("protocol.magic must be RMON")
    if protocol.get("version") != 1 or protocol.get("frame_size") != 32:
        errors.append("protocol version/frame_size must be 1/32")
    if protocol.get("integrity") != "CRC32C":
        errors.append("protocol.integrity must be CRC32C")
    if protocol.get("freshness") != "boot_nonce_64+sequence_64":
        errors.append("protocol freshness must bind boot nonce and sequence")
    if _string_set(protocol, "fixed_message_types", "protocol", errors) != MESSAGE_TYPES:
        errors.append("protocol.fixed_message_types is incomplete")
    for field in ("reject_replay", "reject_unknown_type", "reject_bad_size",
                  "reject_bad_integrity"):
        if _flag(protocol, field, "protocol", errors) is not True:
            errors.append(f"protocol.{field} must be true")

    for field in ("latched_until_power_cycle_or_authenticated_reset",
                  "safe_output_state_defined", "readback_required"):
        if _flag(fence, field, "fence", errors) is not True:
            errors.append(f"fence.{field} must be true")
    if fence.get("readback_source") != "independent-electrical-sense":
        errors.append("fence.readback_source must be independent electrical sense")
    for field in ("command_echo_is_readback", "watchdog_ack_is_readback"):
        if _flag(fence, field, "fence", errors) is not False:
            errors.append(f"fence.{field} must be false")

    expected_failures = {
        "heartbeat_loss": "fence_then_reset",
        "transport_loss": "fence_then_reset",
        "monitor_power_loss": "outputs_default_safe",
        "monitor_clock_fault": "outputs_default_safe",
        "corrupt_or_replayed_frame": "reject_and_preserve_fence",
        "fence_readback_disagreement": "preserve_fence_and_withhold_reintegration",
        "target_reset_failure": "preserve_fence_and_withhold_reintegration",
    }
    for field, expected in expected_failures.items():
        if failure.get(field) != expected:
            errors.append(f"failure_policy.{field} must be {expected}")

    complete = _flag(qualification, "complete", "qualification", errors)
    repetitions = _positive(qualification, "minimum_campaign_repetitions",
                            "qualification", errors)
    if repetitions < 3:
        errors.append("qualification requires at least three campaign repetitions")
    required = _string_set(qualification, "required_scenarios",
                           "qualification", errors)
    if required != SCENARIOS:
        errors.append("qualification.required_scenarios is incomplete")
    disallowed = _string_set(qualification, "disallowed_evidence_kinds",
                             "qualification", errors)
    if disallowed != DISALLOWED_EVIDENCE:
        errors.append("qualification.disallowed_evidence_kinds is incomplete")
    evidence = qualification.get("physical_evidence")
    if not isinstance(evidence, list):
        errors.append("qualification.physical_evidence must be a list")
        evidence = []
    evidence_scenarios: set[str] = set()
    for index, item in enumerate(evidence, 1):
        prefix = f"qualification.physical_evidence[{index}]"
        if not isinstance(item, dict):
            errors.append(f"{prefix} must be a table")
            continue
        scenario = item.get("scenario")
        if scenario not in SCENARIOS:
            errors.append(f"{prefix}.scenario is unknown")
        elif scenario in evidence_scenarios:
            errors.append(f"{prefix}.scenario is duplicated")
        else:
            evidence_scenarios.add(scenario)
        if item.get("kind") != "physical-target":
            errors.append(f"{prefix}.kind must be physical-target")
        count = item.get("repetitions")
        if not isinstance(count, int) or isinstance(count, bool) or count < repetitions:
            errors.append(f"{prefix}.repetitions is below campaign minimum")
        if item.get("result") != "pass":
            errors.append(f"{prefix}.result must be pass")
        _evidence_file(root, item.get("report"), item.get("report_sha256"),
                       f"{prefix}.report", errors)
        _evidence_file(root, item.get("raw_log"), item.get("raw_log_sha256"),
                       f"{prefix}.raw_log", errors)

    if status == "unbound":
        if target_selected is not False or monitor_selected is not False:
            errors.append("unbound profile cannot select target or monitor")
        if any(target_values + monitor_values) or firmware_sha:
            errors.append("unbound profile cannot carry partial hardware identity")
        if any(value is not False for field, value in independence_values.items()
               if field != "power_loss_defaults_safe"):
            errors.append("unbound profile cannot claim hardware independence")
        if independence_values.get("power_loss_defaults_safe") is not True:
            errors.append("unbound contract must require power-loss-safe outputs")
        if complete is not False or evidence:
            errors.append("unbound profile cannot be qualified or contain evidence")
    elif status in {"selected", "qualified"}:
        if target_selected is not True or monitor_selected is not True:
            errors.append("selected hardware must set both selected flags")
        if not all(target_values + monitor_values):
            errors.append("selected hardware identity is incomplete")
        if not SHA256.fullmatch(firmware_sha):
            errors.append("selected monitor firmware_sha256 is invalid")
        if any(value is not True for value in independence_values.values()):
            errors.append("selected hardware does not satisfy all independence claims")
        if status == "selected" and complete is not False:
            errors.append("selected profile cannot set qualification complete")
        if status == "qualified":
            if complete is not True:
                errors.append("qualified profile must set qualification complete")
            missing = sorted(SCENARIOS - evidence_scenarios)
            if missing:
                errors.append(f"qualified profile lacks physical scenarios: {missing}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("profile", nargs="?",
                        default="safety/external_safety_monitor.toml")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    profile = (root / args.profile).resolve()
    try:
        profile.relative_to(root)
    except ValueError:
        print("external-monitor: profile escapes repository", file=sys.stderr)
        return 1
    errors = validate(profile, root)
    for error in errors:
        print(f"external-monitor: {error}", file=sys.stderr)
    if errors:
        return 1
    status = tomllib.loads(profile.read_text(encoding="utf-8"))["status"]
    print(f"external-monitor: PASS status={status}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
