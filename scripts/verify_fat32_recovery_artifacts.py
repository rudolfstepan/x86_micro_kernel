"""R3.40 actual image kernels, unchanged payloads and journal/CPU hotpaths."""
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path
from run_qemu_math import ROOT, digest, kernel_digest
from verify_file_object_guard_artifacts import program_paths
from verify_js_runner_artifacts import PINNED
from verify_text_artifacts import read_fat_file


def baseline_source(path):
    return subprocess.check_output(["git", "show", "c1130a39:" + path], cwd=ROOT,
        timeout=15).decode("utf-8").replace("\r\n", "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r340-fat32-recovery").resolve()
    if evidence.exists() or evidence == allowed or not evidence.is_relative_to(allowed):
        parser.error("new r340 evidence subdirectory required")
    evidence.mkdir(parents=True)
    report = {"passed": False, "images": {}, "programs": {}, "unchanged_source": []}
    started = time.monotonic()
    try:
        paths = program_paths()
        baseline = json.loads((ROOT / "build/codex-agent/r339-storage-retirement/artifacts/protected.json").read_text())
        if not baseline["passed"] or set(paths) != set(baseline["programs"]) or len(paths) != 93:
            raise ValueError("accepted baseline/program inventory mismatch")
        for name in paths:
            actual = digest(ROOT / "build/programs" / name)
            if actual != baseline["programs"][name] or (name in PINNED and actual != PINNED[name]):
                raise ValueError("program drift " + name)
            report["programs"][name] = actual
        for target, image, kernel in (
            ("qemu", ROOT / "build/reist-os.img", ROOT / "build/kernel.bin"),
            ("vmware", ROOT / "build/vmware/reist-os/reist-os-flat.vmdk", allowed / "kernel-vmware.bin")):
            actual = kernel_digest(image)
            if actual != digest(kernel):
                raise ValueError("kernel/image mismatch " + target)
            for name, expected in report["programs"].items():
                if hashlib.sha256(read_fat_file(image, paths[name])).hexdigest() != expected:
                    raise ValueError("stale packaged program " + target + "/" + name)
            report["images"][target] = {"sha256": digest(image), "kernel_sha256": actual}
        for path in ("kernel/sched/scheduler.c", "kernel/sched/scheduler.h", "arch/x86/include/cpu_local.h"):
            if baseline_source(path) != (ROOT / path).read_text(encoding="utf-8"):
                raise ValueError("protected hotpath source changed " + path)
            report["unchanged_source"].append(path)
        path = "drivers/block/ata_journal.c"
        old = baseline_source(path)
        new = (ROOT / path).read_text(encoding="utf-8")
        if (old.split("bool ata_undo_journal_attach(")[0] != new.split("static bool recovery_target_valid(")[0] or
                old[old.index("bool ata_undo_journal_is_attached("):] != new[new.index("bool ata_undo_journal_is_attached("):]):
            raise ValueError("normal journal read/write/commit hotpath drift")
        report["unchanged_source"].append(path + " outside recovery admission")
        report["passed"] = True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    report["elapsed_seconds"] = round(time.monotonic() - started, 3)
    (evidence / "protected.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("FAT32_RECOVERY_ARTIFACTS " + ("PASS" if report["passed"] else "FAIL: " + report["error"]))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
