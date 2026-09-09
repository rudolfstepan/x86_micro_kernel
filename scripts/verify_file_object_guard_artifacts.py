"""R3.38: actual new kernels in both images, protected PRGs and shell layout."""
import argparse
import hashlib
import json
import re
import time
from pathlib import Path

from run_qemu_math import ROOT, digest, kernel_digest
from verify_js_runner_artifacts import PINNED
from verify_text_artifacts import read_fat_file


def program_paths():
    windows = (ROOT / "scripts/build-windows.ps1").read_text(encoding="utf-8")
    make = (ROOT / "Makefile").read_text(encoding="utf-8")
    paths = {name: path for path, name in re.findall(r"'([^']+)' = '([A-Z0-9]+\.PRG)'", windows)}
    if paths.get("OBJGDTST.PRG") != "bin/objgdtst.prg" or \
            "bin/objgdtst.prg=$(SYSTEM_PROGRAM_DIR)/OBJGDTST.PRG" not in make:
        raise ValueError("guest command missing from either userspace-shell image layout")
    aliases = {}
    for name, path in paths.items():
        stem = Path(path).stem
        alias = path if len(stem) <= 8 else str(Path(path).with_name(stem[:6] + "~1.prg")).replace("\\", "/")
        if alias in aliases.values():
            raise ValueError("ambiguous program alias " + alias)
        aliases[name] = alias
    return aliases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    allowed = (ROOT / "build/codex-agent/r338-file-lifetime").resolve()
    if evidence == allowed or not evidence.is_relative_to(allowed) or evidence.exists():
        parser.error("new evidence subdirectory under r338-file-lifetime required")
    evidence.mkdir(parents=True)
    report = {"passed": False, "images": {}, "programs": {}}
    started = time.monotonic()
    try:
        paths = program_paths()
        for name, expected in PINNED.items():
            if name not in paths or digest(ROOT / "build/programs" / name) != expected:
                raise ValueError("protected program changed " + name)
        for name in paths:
            report["programs"][name] = digest(ROOT / "build/programs" / name)
        for target, image, kernel in (
            ("qemu", ROOT / "build/reist-os.img", ROOT / "build/kernel.bin"),
            ("vmware", ROOT / "build/vmware/reist-os/reist-os-flat.vmdk", allowed / "kernel-vmware-final.bin"),
        ):
            actual = kernel_digest(image)
            if actual != digest(kernel):
                raise ValueError("new target kernel/image mismatch " + target)
            for name, expected in report["programs"].items():
                if hashlib.sha256(read_fat_file(image, paths[name])).hexdigest() != expected:
                    raise ValueError("stale packaged program " + target + "/" + name)
            report["images"][target] = {"sha256": digest(image), "kernel_sha256": actual}
        report["passed"] = True
    except (OSError, ValueError) as error:
        report["error"] = str(error)
    report["elapsed_seconds"] = round(time.monotonic() - started, 3)
    (evidence / "protected.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("FILE_OBJECT_GUARD_ARTIFACTS " + ("PASS" if report["passed"] else "FAIL: " + report["error"]))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
