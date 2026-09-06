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
import time

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


def compile_mixed_host(sources, executable, flags=(), source_flags=None, environment=None):
    """Per-TU languages, C link without C++ runtime; 90s total compile deadline."""
    suppress_windows_test_dialogs()
    from build_user_program import find_zig, cpp_compile_flags
    zig = str(find_zig())
    env = os.environ.copy() if environment is None else environment.copy()
    env.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / "build/codex-agent/r318/zig-global"))
    env.setdefault("ZIG_LOCAL_CACHE_DIR", str(ROOT / "build/codex-agent/r318/zig-local"))
    deadline = time.monotonic() + 90
    objects = []
    for index, source in enumerate(sources):
        obj = Path(executable).with_name(Path(executable).stem + f"-{index}.o")
        language = cpp_compile_flags() if Path(source).suffix == ".cpp" else ["-std=c11"]
        subprocess.run([zig, "cc", *language, "-Iuserspace/cpp/include", *flags,
                        *(source_flags or {}).get(str(source), ()), "-c", str(source), "-o", str(obj)],
                       cwd=ROOT, env=env, check=True, timeout=max(.01, deadline-time.monotonic()))
        objects.append(str(obj))
    subprocess.run([zig, "cc", *objects, "-o", str(executable)], cwd=ROOT, env=env,
                   check=True, timeout=max(.01, deadline-time.monotonic()))
    return objects


RESPONSE_BASELINE = "2e17d5fb8eb414d4676d3a5fbd8592df8e5dd195"
RESPONSE_C = "userspace/gui/apps/browser/browser_response.c"
RESPONSE_CPP = "userspace/gui/apps/browser/browser_response.cpp"
RESPONSE_FLAGS = ["-O2", "-UNDEBUG", "-fno-builtin", "-Wall", "-Wextra", "-Werror",
                  "-I.", "-Iuserspace/gui/include", "-Iuserspace/gui/apps/browser"]
BENCH_DEPENDENCIES = ["userspace/programs/curl_http.c", "userspace/gui/lib/html_document.c",
                      "userspace/gui/lib/control.c", "userspace/gui/compositor/desktop_wm.c"]


def paired_response_benchmark(directory):
    """R3.18 frozen oracle and bounds; record all samples before checking limits."""
    from build_user_program import find_zig, cpp_compile_flags
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    baseline = directory / "response-baseline.c"
    baseline.write_bytes(git("show", RESPONSE_BASELINE + ":" + RESPONSE_C))
    fixture = "test/test_cpp_baseline_bench.c"
    digests = {}
    for path in [fixture, *BENCH_DEPENDENCIES]:
        data = (ROOT / path).read_bytes().replace(b"\r\n", b"\n")
        if data != git("show", RESPONSE_BASELINE + ":" + path).replace(b"\r\n", b"\n"):
            raise ValueError("paired fixture/dependency drift: " + path)
        digests[path] = hashlib.sha256(data).hexdigest()
    for path in [RESPONSE_CPP, "userspace/gui/apps/browser/browser_response.hpp",
                 "userspace/gui/apps/browser/browser_response.h"]:
        digests[path] = hashlib.sha256((ROOT/path).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
    executables = [directory / "baseline.exe", directory / "candidate.exe"]
    for response, executable in zip([baseline, RESPONSE_CPP], executables):
        compile_mixed_host([fixture, response, *BENCH_DEPENDENCIES], executable, RESPONSE_FLAGS)
    pairs = []
    for _ in range(5):
        pairs.append([json.loads(subprocess.check_output([str(exe)], cwd=ROOT, timeout=15))
                      for exe in executables])
    medians = [statistics.median(pair[i]["browser_response_ns"] for pair in pairs) for i in range(2)]
    result = {"baseline_commit": RESPONSE_BASELINE,
              "baseline_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "source_fixture_sha256_lf": digests, "platform": platform.platform(),
              "processor": platform.processor(), "machine": platform.machine(),
              "compiler": subprocess.check_output([str(find_zig()), "version"], timeout=10).decode().strip(),
              "flags": RESPONSE_FLAGS, "cpp_flags": cpp_compile_flags(), "lto": False,
              "iterations_per_sample": 200000, "process_deadline_seconds": 15,
              "clock": "QueryPerformanceCounter outside loop", "pairs_c_cpp": pairs,
              "median_c_ns": medians[0], "median_cpp_ns": medians[1],
              "ratio": medians[1]/medians[0], "limits": {"ratio": 1.2, "cpp_ns": 5000}}
    (directory / "paired-response.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    if os.name != "nt" or result["compiler"] != "0.16.0":
        raise ValueError("frozen Windows/Zig 0.16 measurement profile unavailable")
    if any(any(not 0 < s["browser_response_ns"] for s in pair) for pair in pairs):
        raise ValueError("invalid response timing sample")
    if result["ratio"] > 1.2 or medians[1] > 5000:
        raise ValueError("frozen response timing bound exceeded: " + json.dumps(result))
    return result


def benchmark():
    suppress_windows_test_dialogs()
    from build_user_program import find_zig
    zig = str(find_zig())
    sources = ["test/test_cpp_baseline_bench.c", RESPONSE_C if (ROOT/RESPONSE_C).exists() else RESPONSE_CPP,
               "userspace/programs/curl_http.c", "userspace/gui/lib/html_document.c",
               "userspace/gui/lib/control.c", "userspace/gui/compositor/desktop_wm.c"]
    with tempfile.TemporaryDirectory(prefix="reist-cpp-baseline-") as tmp:
        executable = Path(tmp) / "bench.exe"
        env = os.environ.copy()
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/cpp-baseline-cache")
        env["ZIG_LOCAL_CACHE_DIR"] = str(Path(tmp) / "cache")
        flags = ["cc", "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-I.", "-Iuserspace/gui/include"]
        compile_mixed_host(sources, executable, flags[2:], environment=env)
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
