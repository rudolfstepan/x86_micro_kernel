"""R3.41 reference kernels/payloads and unchanged protected runtime paths."""
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path
from run_qemu_math import ROOT, digest, kernel_digest
from verify_file_object_guard_artifacts import program_paths
from verify_text_artifacts import read_fat_file
import sys
sys.path.insert(0, str(ROOT / "test"))
from test_reist_probe_domain import function

NETWORK_BASELINE = "996cff15624e8f1534003d9073073bdfcee5b233"
NETWORK_SHA256 = "3c19e56bd480c8da9d9dfa8b08bb55659f0c137f3cf154a4476aeb22236b7346"

ATA_DEADLINE_EXISTING = (
    "static bool ata_pio_select_read(", "static int ata_pio_read_block_size(",
    "static bool ata_program_pio_batch(", "static bool ata_read_sectors_pio_impl(",
    "static bool ata_write_sectors_pio_deferred_impl(", "static bool ata_flush_cache_impl(")
ATA_DEADLINE_NEW = (
    "static bool ata_transaction_begin_until(", "static bool ata_read_sectors_pio_until(",
    "static bool ata_write_sectors_pio_deferred_until(", "static bool ata_flush_cache_until(",
    "int ata_external_journal_handoff(", "int ata_external_journal_io(")


def ata_protected_source_equal(old, new):
    """Only explicitly approved deadline bodies differ; no whole-file waiver."""
    for signature in ATA_DEADLINE_EXISTING:
        if old.count(signature) != 1 or new.count(signature) != 1: return False
        old = old.replace(function(old, signature), "", 1)
        new = new.replace(function(new, signature), "", 1)
    for signature in ATA_DEADLINE_NEW:
        if signature in old or new.count(signature) != 1: return False
        new = new.replace(function(new, signature), "", 1)
    return old.split() == new.split()


def worker_probe_delta(old, new):
    """Only the two immediate bounds of the existing restricted INT80 probe
    extend130->131. Engine/host code and all other image bytes stay exact."""
    if len(old) != len(new) or len(old) < 4609:
        return False
    if old[4497:4503] != bytes.fromhex("81fb82000000") or old[4604:4609] != bytes.fromhex("6882000000"):
        return False
    expected = bytearray(old)
    expected[4499] = expected[4605] = 131
    return expected == new


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--vmware-kernel", type=Path,
        default=ROOT / "build/codex-agent/r341-journal-handoff/kernel-vmware.bin")
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r341-journal-handoff").resolve()
    if evidence.exists() or evidence == allowed or not evidence.is_relative_to(allowed):
        parser.error("new r341 evidence subdirectory required")
    evidence.mkdir(parents=True)
    report = {"passed": False, "images": {}, "programs": {}, "unchanged_source": []}
    started = time.monotonic()
    try:
        baseline = json.loads((ROOT / "build/codex-agent/r340-fat32-recovery/artifacts/protected.json").read_text())
        paths = program_paths()
        if not baseline["passed"] or len(paths) != 93 or set(paths) != set(baseline["programs"]):
            raise ValueError("accepted93-program baseline mismatch")
        # Only the independently accepted ARP prerequisite changes this one
        # protected payload. All other R3.40 expectations remain unchanged.
        baseline["programs"]["REIST.PRG"] = NETWORK_SHA256
        report["network_baseline"] = NETWORK_BASELINE
        for name in paths:
            actual = digest(ROOT / "build/programs" / name)
            if name == "JSWORK.PRG" and actual != baseline["programs"][name]:
                old = (ROOT / "build/codex-agent/r340-fat32-recovery/accepted-reference/programs/JSWORK.PRG").read_bytes()
                new = (ROOT / "build/programs/JSWORK.PRG").read_bytes()
                if hashlib.sha256(old).hexdigest() != baseline["programs"][name] or not worker_probe_delta(old, new):
                    raise ValueError("worker change outside exact restricted-probe bounds")
                report["js_worker_probe_extension"] = {"offsets": [4499, 4605], "from": 130, "to": 131}
            elif name != "STORAGE.PRG" and actual != baseline["programs"][name]:
                raise ValueError("protected program drift " + name)
            report["programs"][name] = actual
        for target, image, kernel in (
            ("qemu", ROOT / "build/reist-os.img", ROOT / "build/kernel.bin"),
            ("vmware", ROOT / "build/vmware/reist-os/reist-os-flat.vmdk", args.vmware_kernel.resolve())):
            actual = kernel_digest(image)
            if actual != digest(kernel):
                raise ValueError("kernel/image mismatch " + target)
            for name, expected in report["programs"].items():
                if hashlib.sha256(read_fat_file(image, paths[name])).hexdigest() != expected:
                    raise ValueError("stale packaged program " + target + "/" + name)
            report["images"][target] = {"sha256": digest(image), "kernel_sha256": actual}
        for path in ("kernel/sched/scheduler.c", "kernel/sched/scheduler.h", "arch/x86/include/cpu_local.h",
                     "drivers/block/ata_journal.c", "drivers/block/ata.c", "kernel/proc/process.c", "userspace/js/js_worker.c"):
            old = subprocess.check_output(["git", "show", "7d87119c:" + path], cwd=ROOT,
                timeout=15).decode("utf-8").replace("\r\n", "\n")
            new = (ROOT / path).read_text(encoding="utf-8")
            if path == "drivers/block/ata.c":
                if not ata_protected_source_equal(old, new):
                    raise ValueError("ATA change outside approved deadline helpers")
                report["ata_deadline_helpers"] = list(ATA_DEADLINE_EXISTING + ATA_DEADLINE_NEW)
                report["unchanged_source"].append(path + " outside approved deadline helpers")
                continue
            if path == "kernel/proc/process.c":
                for signature in ("static bool initialize_domain_profile(",):
                    new = new.replace(function(new, signature), "")
                    old = old.replace(function(old, signature), "")
            if old.split() != new.split():
                raise ValueError("protected runtime source changed " + path)
            report["unchanged_source"].append(path)
        release = (ROOT / "build/programs/STORAGE.PRG").read_bytes()
        for marker in (b"__handoff", b"HANDOFF_RECOVERY_COMMIT_OK", b"HANDOFF_FAILED"):
            if marker in release:
                raise ValueError("private trigger in production Storage")
        report["passed"] = True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
    report["elapsed_seconds"] = round(time.monotonic() - started, 3)
    (evidence / "protected.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("JOURNAL_HANDOFF_ARTIFACTS " + ("PASS" if report["passed"] else "FAIL: " + report["error"]))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
