"""Version-1 lexical inventory and bounded real-code host benchmark, not an AST/WCET proof."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ROOTS = {
    "browser": ["userspace/gui/apps/browser"],
    "gui_source_tree": ["userspace/gui/lib", "userspace/gui/include"],
    "compositor": ["userspace/gui/compositor"],
}
TOKENS = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
PATTERNS = {
    "if_tokens": r"\bif\s*\(", "goto_tokens": r"\bgoto\b",
    "negative_return_tokens": r"\breturn\s+-",
    "struct_typedefs": r"\btypedef\s+struct\b",
    "opaque_typedefs": r"\btypedef\s+struct\s+\w+\s+\w+\s*;",
    "state_pointer_mentions": r"\b\w*(?:state|model|manager)\w*_t\s*\*",
}


def suppress_windows_test_dialogs():
    """Only this runner and inheriting children; never change registry/system policy.

    Microsoft SetErrorMode documents child inheritance; CREATE_DEFAULT_ERROR_MODE
    must not be used. WerSetFlags(NO_UI) also suppresses this runner's WER UI.
    Failure to establish the mode aborts before launching any native test.
    """
    if os.name != "nt":
        return
    import ctypes
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetErrorMode.argtypes = []
    kernel.GetErrorMode.restype = wintypes.UINT
    kernel.SetErrorMode.argtypes = [wintypes.UINT]
    kernel.SetErrorMode.restype = wintypes.UINT
    kernel.WerSetFlags.argtypes = [wintypes.DWORD]
    kernel.WerSetFlags.restype = wintypes.LONG
    required = 0x8003  # FAILCRITICALERRORS | NOGPFAULTERRORBOX | NOOPENFILEERRORBOX
    kernel.SetErrorMode(kernel.GetErrorMode() | required)
    if kernel.GetErrorMode() & required != required or kernel.WerSetFlags(32) != 0:
        raise RuntimeError("noninteractive Windows test mode unavailable")


def measure_text(text):
    text = text.replace("\r\n", "\n")
    # Preserve line boundaries; literal contents cannot become identifier hits.
    code = TOKENS.sub(lambda m: ("" if m[0].startswith("/") else '""') + "\n" * m[0].count("\n"), text)
    names = re.findall(r"\b([A-Za-z_]\w*)\s*\(", code)
    return {
        "physical_lines": len(text.splitlines()),
        "nonblank_lines": sum(bool(line.strip()) for line in text.splitlines()),
        "code_lines": sum(bool(line.strip()) for line in code.splitlines()),
        **{key: len(re.findall(pattern, code)) for key, pattern in PATTERNS.items()},
        "init_mentions": dict(sorted(Counter(n for n in names if re.search(r"(?:^|_)(?:init|initialize|open|create|acquire|connect)$", n)).items())),
        "cleanup_mentions": dict(sorted(Counter(n for n in names if re.search(r"(?:^|_)(?:free|destroy|release|close|cleanup|reap|shutdown)$", n)).items())),
    }


def artifact_info(name, data):
    info = {"path": name, "file_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    if name.lower().endswith(".prg"):
        if len(data) < 28:
            raise ValueError("truncated MYPR")
        ident, magic, entry, size, base, reloc, count = struct.unpack_from("<4s6I", data)
        if ident != b"MYPR" or magic != 0xDEADBEEF or count or reloc != len(data) or not 28 <= entry < len(data):
            raise ValueError("invalid baseline MYPR artifact")
        info["loader_payload_bytes"] = size
    return info


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, timeout=15)


def inventory(revision):
    if not re.fullmatch(r"[0-9a-fA-F]{7,40}", revision):
        raise ValueError("revision must be a commit hash")
    revision = git("rev-parse", "--verify", revision + "^{commit}").decode().strip()
    components = {}
    for component, roots in ROOTS.items():
        paths = sorted(p for p in git("ls-tree", "-r", "--name-only", revision, "--", *roots).decode().splitlines()
                       if Path(p).suffix.lower() in (".c", ".h", ".s", ".cpp", ".hpp"))
        if not 0 < len(paths) <= 256:
            raise ValueError("source inventory capacity")
        files = []; totals = Counter(); initializers = Counter(); cleanup = Counter()
        for path in paths:
            data = git("show", revision + ":" + path)
            if len(data) > 2 * 1024 * 1024:
                raise ValueError("source byte capacity")
            current = (ROOT / path).read_bytes().replace(b"\r\n", b"\n")
            if current != data.replace(b"\r\n", b"\n"):
                raise ValueError("working source differs from baseline: " + path)
            metrics = measure_text(data.decode("utf-8"))
            initializers.update(metrics.pop("init_mentions")); cleanup.update(metrics.pop("cleanup_mentions"))
            totals.update(metrics)
            files.append({"path": path, "git_blob": git("rev-parse", revision + ":" + path).decode().strip(), **metrics})
        components[component] = {"files": files, "totals": dict(totals),
                                 "init_mentions": dict(sorted(initializers.items())),
                                 "cleanup_mentions": dict(sorted(cleanup.items()))}
    return revision, components


def benchmark():
    suppress_windows_test_dialogs()
    from build_user_program import find_zig
    zig = str(find_zig())
    sources = ["test/test_cpp_baseline_bench.c", "userspace/gui/apps/browser/browser_response.c",
               "userspace/programs/curl_http.c", "userspace/gui/lib/html_document.c",
               "userspace/gui/lib/control.c", "userspace/gui/compositor/desktop_wm.c"]
    with tempfile.TemporaryDirectory(prefix="reist-cpp-baseline-") as tmp:
        executable = Path(tmp) / "bench.exe"
        env = os.environ.copy()
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/cpp-baseline-cache")
        env["ZIG_LOCAL_CACHE_DIR"] = str(Path(tmp) / "cache")
        flags = ["cc", "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-I.", "-Iuserspace/gui/include"]
        subprocess.run([zig, *flags, *sources, "-o", str(executable)], cwd=ROOT, env=env, check=True, timeout=90)
        samples = [json.loads(subprocess.check_output([str(executable)], cwd=ROOT, timeout=15)) for _ in range(5)]
    keys = {"browser_response_ns", "gui_dispatch_ns", "wm_dispatch_ns"}
    if any(set(s) != keys or any(v <= 0 for v in s.values()) for s in samples):
        raise ValueError("invalid timing samples")
    return {"compiler": subprocess.check_output([zig, "version"], timeout=10).decode().strip(),
            "flags": flags, "platform": platform.platform(), "machine": platform.machine(),
            "iterations_per_sample": 200000, "samples": samples,
            "median_ns_per_operation": {k: statistics.median(s[k] for s in samples) for k in sorted(keys)}}


def main():
    if sys.argv[1:2] == ["--host-test"]:
        # Execute the unchanged Python gate with inherited noninteractive mode.
        # Assertions/errors/status stay authoritative; only UI is suppressed.
        suppress_windows_test_dialogs()
        if len(sys.argv) < 3:
            raise ValueError("missing host-test arguments")
        result = subprocess.run([sys.executable, *sys.argv[2:]], cwd=ROOT,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
            capture_output=True, text=True, timeout=600)
        sys.stdout.write(result.stdout); sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    revision, components = inventory(args.revision)
    artifacts = [artifact_info(p, (args.artifacts / p).read_bytes()) for p in
                 ("programs/BROWSER.PRG", "programs/HTMLWORK.PRG", "programs/DESKTOP.PRG", "sdk/usr/lib/libreistgui.a")]
    result = {"schema": 1, "source_commit": revision, "components": components,
              "artifacts": artifacts, "host_benchmark": benchmark()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"source_commit": revision, "totals": {k: v["totals"] for k, v in components.items()},
                      "artifacts": artifacts, "host_benchmark": result["host_benchmark"]}, indent=2))


if __name__ == "__main__":
    main()
