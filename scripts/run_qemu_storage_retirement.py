"""R3.39: unchanged lifetime fixtures plus generation-ordered SMP retirement."""
import argparse
import json
import re
import time
from pathlib import Path

import run_qemu_file_object_guard as guard
from measure_cpp_baseline import suppress_windows_test_dialogs
from run_qemu_math import ROOT, digest


def exact_shuffle(text, left, right):
    """Two complete known serial records, no dropped/reordered/extra bytes.

    Fixed small DP: each source's byte order is retained; this is not a fuzzy
    subsequence search through arbitrary output. Unknown third writers fail.
    """
    if max(len(left), len(right)) > 192 or len(text) != len(left) + len(right):
        return False
    row = [False] * (len(right) + 1)
    row[0] = True
    for j in range(1, len(row)):
        row[j] = row[j-1] and right[j-1] == text[j-1]
    for i in range(1, len(left) + 1):
        row[0] = row[0] and left[i-1] == text[i-1]
        for j in range(1, len(row)):
            char = text[i+j-1]
            row[j] = (row[j] and left[i-1] == char) or (row[j-1] and right[j-1] == char)
    return row[-1]


def ap_execution(window, generation):
    desktop = "REIST_GUI DESKTOP_AUTOSTART_DISABLED explicit DESKTOP command required\n"
    lines = window.splitlines(keepends=True)
    candidates = 0
    for cpu in range(1, 4):
        expected = f"REIST_STORAGE SERVICE_AP_EXEC cpu={cpu} generation={generation}\n"
        if re.search(r"(?:^|\n)(?:C:\\>)?" + re.escape(expected), window):
            return True
        for index in range(len(lines)-1):
            joined = lines[index] + lines[index+1]
            if len(joined) != len(desktop) + len(expected):
                continue
            candidates += 1
            if candidates > 64:
                return False
            if exact_shuffle(joined, desktop, expected):
                return True
    return False


def validate_retirement(text, cpus):
    identities = list(re.finditer(r"(?:^|\n)(?:C:\\>)?REIST_STORAGE SERVICE_IDENTITY pid=(\d+) generation=(\d+)\r?(?=\n|$)", text))
    if len(identities) < 2:
        return "missing replacement identity"
    for old, new in zip(identities, identities[1:]):
        if old.groups() == new.groups():
            return "reused Storage identity"
        record = "REIST_STORAGE SERVICE_RETIRED pid=" + old[1] + " generation=" + old[2]
        pattern = r"(?:^|\n)(?:C:\\>)?" + re.escape(record) + r"\r?(?=\n|$)"
        if not re.search(pattern, text[old.end():new.start()]):
            return "replacement before exact old generation reap"
    if cpus > 1:
        if "REIST_SMP SCHEDULER_READY cpus=4" not in text:
            return "four CPU scheduler not proven"
        # The old manual down/up contract clears the desired AP mask. Require
        # AP execution for the original and automatic replacement, not an
        # invented affinity promise for the later manual administrative reset.
        for index in (0, 1):
            identity = identities[index]
            end = identities[index+1].start()+1 if index+1 < len(identities) else len(text)
            if not ap_execution(text[identity.end():end], identity[2]):
                return "old/replacement Storage AP execution missing"
    if "RETIREMENT_EXHAUSTED" in text:
        return "unexpected retirement exhaustion"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qemu", "image", "evidence"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r339-storage-retirement").resolve()
    if evidence.exists() or evidence == allowed or not evidence.is_relative_to(allowed):
        parser.error("new r339 evidence subdirectory required")
    evidence.mkdir(parents=True)
    suppress_windows_test_dialogs()
    guard.smoke.failure_marker = guard.guard_failure_marker
    guard.smoke.exact_line_position = guard.guard_line_position
    reference = digest(args.image)
    started = time.monotonic()
    original_command = guard.ext2.qemu_command
    report = {"passed": False, "cases": [], "reference_sha256": reference}
    try:
        targets = {0: args.image}
        for mode in (1, 2):
            targets[mode] = guard.private_image(args.image, evidence, mode)
        for cpus, mode, name in ((1, 0, "normal"), (1, 1, "fault"), (1, 2, "hang"),
                                 (4, 1, "smp-fault"), (4, 2, "smp-hang")):
            guard.ext2.qemu_command = lambda q, i, d: original_command(q, i, d) + ["-smp", str(cpus)]
            result = guard.run_case(args.qemu, targets[mode], evidence, name, mode,
                                    min(started + 450, time.monotonic() + 180))
            raw = (evidence / (name + ".log")).read_text(encoding="utf-8")
            result["cpus"] = cpus
            result["retirement_error"] = validate_retirement(raw, cpus)
            result["passed"] = result["passed"] and result["retirement_error"] is None
            report["cases"].append(result)
            if not result["passed"]:
                break
        report["passed"] = len(report["cases"]) == 5 and all(case["passed"] for case in report["cases"])
    except Exception as error:
        report["error"] = str(error)
    finally:
        guard.ext2.qemu_command = original_command
        if digest(args.image) != reference:
            report["passed"] = False
            report["error"] = "reference image changed"
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (evidence / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("STORAGE_RETIREMENT_GUEST " + ("PASS" if report["passed"] else "FAIL"), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
