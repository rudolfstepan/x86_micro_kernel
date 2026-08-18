#!/usr/bin/env python3
"""Run one or more verified, bounded REIST Codex packages."""

from __future__ import annotations

import argparse
import copy
import contextlib
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import tomllib
from typing import Any


class VerificationError(RuntimeError):
    pass


class GateFailure(VerificationError):
    def __init__(self, gate: str, log_path: pathlib.Path, exit_code: int):
        self.gate = gate
        self.log_path = log_path
        self.exit_code = exit_code
        super().__init__(
            f"gate failed ({exit_code}): {gate}; log={log_path}\n{tail(log_path)}"
        )


def copy_result_file(source: pathlib.Path, destination: pathlib.Path) -> None:
    if source.resolve() != destination.resolve():
        shutil.copyfile(source, destination)


class PackageTimeout(RuntimeError):
    pass


WINDOWS_CODEX_HELPERS = (
    "codex-command-runner.exe",
    "codex-windows-sandbox-setup.exe",
)


def git(repo: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        details = "\n".join(
            part for part in (result.stdout.strip(), result.stderr.strip()) if part
        )
        raise VerificationError(
            f"git {' '.join(arguments)} failed: {details}"
        )
    return result.stdout.strip()


def assert_clean(repo: pathlib.Path) -> None:
    status = git(repo, "status", "--porcelain")
    if status:
        raise VerificationError(f"worktree is not clean:\n{status}")


def read_task(path: pathlib.Path) -> dict[str, Any]:
    with path.open("rb") as stream:
        return tomllib.load(stream)


def active_package(task: dict[str, Any]) -> dict[str, Any] | None:
    active_id = task.get("active_id", "")
    matches = [
        package
        for package in task.get("packages", [])
        if package.get("status") == "active"
    ]
    if not active_id:
        if matches:
            raise VerificationError("active_id is empty but an active package exists")
        return None
    if len(matches) != 1 or matches[0].get("id") != active_id:
        raise VerificationError("active_id does not identify exactly one active package")
    return matches[0]


def expected_task_after_success(
    before: dict[str, Any], after: dict[str, Any]
) -> dict[str, Any]:
    current = active_package(before)
    if current is None:
        raise VerificationError("a commit was reported without an active package")
    expected = copy.deepcopy(before)
    current_index = next(
        index
        for index, package in enumerate(expected["packages"])
        if package["id"] == current["id"]
    )
    after_by_id = {package["id"]: package for package in after.get("packages", [])}
    if current["id"] not in after_by_id:
        raise VerificationError("the completed package disappeared from the task queue")
    evidence = after_by_id[current["id"]].get("evidence")
    if not isinstance(evidence, list) or not evidence:
        raise VerificationError("the completed package has no compact gate evidence")
    expected["packages"][current_index]["status"] = "done"
    expected["packages"][current_index]["evidence"] = evidence

    queued_index = next(
        (
            index
            for index, package in enumerate(expected["packages"])
            if package.get("status") == "queued"
        ),
        None,
    )
    if queued_index is None:
        expected["active_id"] = ""
    else:
        expected["packages"][queued_index]["status"] = "active"
        expected["active_id"] = expected["packages"][queued_index]["id"]
    return expected


def verify_result(
    repo: pathlib.Path,
    before_head: str,
    before_task: dict[str, Any],
    result: dict[str, Any],
    task_path: pathlib.Path,
    *,
    allow_dirty_blocked: bool = False,
) -> str:
    status = result.get("status")
    package = active_package(before_task)
    expected_package_id = package["id"] if package else ""
    after_head = git(repo, "rev-parse", "HEAD")

    if status in {"blocked", "no_work"}:
        if after_head != before_head:
            raise VerificationError(f"{status} result changed HEAD")
        if result.get("commit"):
            raise VerificationError(f"{status} result reported a commit")
        if status == "no_work" and package is not None:
            raise VerificationError("no_work is invalid while a package is active")
        if status == "no_work" and result.get("package_id"):
            raise VerificationError("no_work must not name a package")
        if status == "blocked" and result.get("package_id") != expected_package_id:
            raise VerificationError("blocked result names the wrong package")
        if status == "blocked" and not str(result.get("blocker", "")).strip():
            raise VerificationError("blocked result has no concrete blocker")
        if status == "no_work" or not allow_dirty_blocked:
            assert_clean(repo)
        return after_head

    if status != "committed":
        raise VerificationError(f"unknown result status {status!r}")
    if package is None or result.get("package_id") != expected_package_id:
        raise VerificationError("committed result names the wrong package")
    reported_commit = result.get("commit", "")
    if not re.fullmatch(r"[0-9a-f]{40}", reported_commit):
        raise VerificationError("committed result must contain the full lowercase SHA-1")
    if after_head != reported_commit:
        raise VerificationError("reported commit does not equal HEAD")
    if git(repo, "rev-list", "--count", f"{before_head}..{after_head}") != "1":
        raise VerificationError("a package must create exactly one commit")
    if git(repo, "rev-parse", f"{after_head}^") != before_head:
        raise VerificationError("the package commit is not a direct child of the baseline")
    subject = git(repo, "show", "-s", "--format=%s", after_head)
    if subject != package["commit_message"]:
        raise VerificationError(
            f"commit subject {subject!r} != {package['commit_message']!r}"
        )

    changed = set(
        filter(
            None,
            git(
                repo,
                "diff-tree",
                "--no-commit-id",
                "--name-only",
                "-r",
                after_head,
            ).splitlines(),
        )
    )
    allowed = {path.replace("\\", "/") for path in package["allowed_files"]}
    outside = sorted(changed - allowed)
    if outside:
        raise VerificationError(f"commit changed files outside package scope: {outside}")

    after_task = read_task(task_path)
    if after_task != expected_task_after_success(before_task, after_task):
        raise VerificationError("task queue transition changed more than status/evidence")
    passed = result.get("passed")
    if not isinstance(passed, list) or not passed:
        raise VerificationError("committed result has no passed gates")
    after_completed = next(
        item for item in after_task["packages"] if item["id"] == expected_package_id
    )
    gates = required_gates(package)
    if passed != gates or after_completed["evidence"] != gates:
        raise VerificationError("result and task evidence must list every gate in order")
    if result.get("blocker"):
        raise VerificationError("committed result must not report a blocker")
    assert_clean(repo)
    return after_head


def kill_process_tree(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    if process.poll() is None:
        process.kill()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass


def run_bounded(
    command: list[str],
    cwd: pathlib.Path,
    prompt: str,
    log_path: pathlib.Path,
    timeout_seconds: int | float,
    env: dict[str, str] | None = None,
) -> int:
    creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=log,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            creationflags=creationflags,
            start_new_session=os.name != "nt",
        )
        try:
            process.communicate(prompt, timeout=timeout_seconds)
        except subprocess.TimeoutExpired as error:
            kill_process_tree(process)
            raise PackageTimeout(
                f"agent exceeded the {timeout_seconds:g}s package deadline"
            ) from error
        return process.returncode


def required_gates(package: dict[str, Any]) -> list[str]:
    return [
        *package["targeted_tests"],
        *package["package_tests"],
        *package["runtime_tests"],
    ]


def canonicalize_task_evidence(
    task_path: pathlib.Path, package_id: str, gates: list[str]
) -> None:
    text = task_path.read_text("utf-8")
    markers = list(re.finditer(r"(?m)^\[\[packages\]\]\s*$", text))
    target_start = None
    target_end = None
    for index, marker in enumerate(markers):
        end = markers[index + 1].start() if index + 1 < len(markers) else len(text)
        block = text[marker.start():end]
        identifier = re.search(r'(?m)^id = "([^"]+)"\s*$', block)
        if identifier and identifier.group(1) == package_id:
            target_start, target_end = marker.start(), end
            break
    if target_start is None or target_end is None:
        raise VerificationError(f"active package {package_id!r} is missing from task")
    block = text[target_start:target_end]
    evidence = re.search(r"(?ms)^evidence = \[[^\]]*\]\s*", block)
    if evidence is None:
        raise VerificationError(f"package {package_id!r} has no evidence field")
    newline = "\r\n" if "\r\n" in text else "\n"
    rendered = "evidence = [" + newline
    for gate in gates:
        rendered += "  " + json.dumps(gate, ensure_ascii=False) + "," + newline
    rendered += "]" + newline
    updated_block = block[:evidence.start()] + rendered + block[evidence.end():]
    task_path.write_text(
        text[:target_start] + updated_block + text[target_end:],
        "utf-8",
        newline="",
    )


def materialize_candidate(
    worktree: pathlib.Path,
    before_head: str,
    before_task: dict[str, Any],
    package: dict[str, Any],
    task_path: pathlib.Path,
    result: dict[str, Any],
) -> str:
    if result.get("status") != "candidate":
        raise VerificationError("agent success must be returned as candidate")
    if result.get("package_id") != package["id"]:
        raise VerificationError("candidate names the wrong package")
    if result.get("commit") or result.get("passed") or result.get("blocker"):
        raise VerificationError("candidate must not claim commit, gates or blocker")
    if git(worktree, "rev-parse", "HEAD") != before_head:
        raise VerificationError("agent changed HEAD before candidate verification")

    gates = required_gates(package)
    canonicalize_task_evidence(task_path, package["id"], gates)
    git(worktree, "add", "-A")
    changed = set(
        filter(None, git(worktree, "diff", "--cached", "--name-only").splitlines())
    )
    if not changed:
        raise VerificationError("candidate contains no changes")
    allowed = {path.replace("\\", "/") for path in package["allowed_files"]}
    outside = sorted(changed - allowed)
    if outside:
        raise VerificationError(f"candidate changed files outside package scope: {outside}")
    after_task = read_task(task_path)
    if after_task != expected_task_after_success(before_task, after_task):
        raise VerificationError("candidate task transition changed more than status/evidence")

    git(worktree, "commit", "-m", package["commit_message"])
    result["status"] = "committed"
    result["commit"] = git(worktree, "rev-parse", "HEAD")
    result["passed"] = gates
    assert_clean(worktree)
    return result["commit"]


def amend_repair_candidate(
    worktree: pathlib.Path,
    candidate_head: str,
    package: dict[str, Any],
    task_path: pathlib.Path,
    result: dict[str, Any],
) -> str:
    if result.get("status") != "candidate":
        raise VerificationError("repair must be returned as candidate")
    if result.get("package_id") != package["id"]:
        raise VerificationError("repair names the wrong package")
    if result.get("commit") or result.get("passed") or result.get("blocker"):
        raise VerificationError("repair must not claim commit, gates or blocker")
    if git(worktree, "rev-parse", "HEAD") != candidate_head:
        raise VerificationError("repair agent changed candidate HEAD")
    git(worktree, "add", "-A")
    changed = set(
        filter(None, git(worktree, "diff", "--cached", "--name-only").splitlines())
    )
    if not changed:
        raise VerificationError("repair contains no changes")
    task_relative = task_path.relative_to(worktree).as_posix()
    if task_relative in changed:
        raise VerificationError("repair changed the already-verified task transition")
    allowed = {path.replace("\\", "/") for path in package["allowed_files"]}
    outside = sorted(changed - allowed)
    if outside:
        raise VerificationError(f"repair changed files outside package scope: {outside}")
    git(worktree, "commit", "--amend", "--no-edit")
    result["status"] = "committed"
    result["commit"] = git(worktree, "rev-parse", "HEAD")
    result["passed"] = required_gates(package)
    assert_clean(worktree)
    return result["commit"]


def scoped_regular_file(repo: pathlib.Path, relative: str) -> pathlib.Path:
    root = repo.resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or not path.is_file() or path.is_symlink():
        raise VerificationError(f"gate file is not a scoped regular file: {relative}")
    return path


def trusted_gate_command(
    gate: str, repo: pathlib.Path, environment: dict[str, str]
) -> list[str]:
    python_gate = re.fullmatch(r"python (test/[A-Za-z0-9_.-]+\.py) -q", gate)
    if python_gate:
        test_file = scoped_regular_file(repo, python_gate.group(1))
        return [str(pathlib.Path(sys.executable).resolve()), str(test_file), "-q"]

    package_gate = re.fullmatch(
        r"\.\\scripts\\test-reist-package\.ps1 "
        r"-Target (qemu|vmware) -Video (vga|framebuffer)",
        gate,
    )
    runtime_gate = re.fullmatch(
        r"\.\\scripts\\test-reist-runtime\.ps1 "
        r"-Mode (normal|pit|watchdog|memory)",
        gate,
    )
    if package_gate or runtime_gate:
        powershell = shutil.which("pwsh", path=environment.get("PATH"))
        if powershell is None:
            raise VerificationError("PowerShell 7 is required for REIST gates")
        if package_gate:
            script = scoped_regular_file(repo, "scripts/test-reist-package.ps1")
            return [
                powershell,
                "-NoProfile",
                "-NonInteractive",
                "-File",
                str(script),
                "-Target",
                package_gate.group(1),
                "-Video",
                package_gate.group(2),
            ]
        script = scoped_regular_file(repo, "scripts/test-reist-runtime.ps1")
        return [
            powershell,
            "-NoProfile",
            "-NonInteractive",
            "-File",
            str(script),
            "-Mode",
            runtime_gate.group(1),
        ]
    raise VerificationError(f"unsupported gate command: {gate!r}")


def run_sandboxed_gates(
    codex: str,
    package: dict[str, Any],
    worktree: pathlib.Path,
    log_root: pathlib.Path,
    timeout_seconds: int | float,
    environment: dict[str, str],
) -> list[str]:
    gates = required_gates(package)
    for index, gate in enumerate(gates, 1):
        gate_log = log_root / f"gate-{index}.log"
        command = [
            codex,
            "sandbox",
            "-P",
            ":workspace",
            "--include-managed-config",
            "-C",
            str(worktree),
            *trusted_gate_command(gate, worktree, environment),
        ]
        exit_code = run_bounded(
            command,
            worktree,
            "",
            gate_log,
            timeout_seconds,
            environment,
        )
        if exit_code != 0:
            raise GateFailure(gate, gate_log, exit_code)
    return gates


def resolve_qemu(environment: dict[str, str]) -> None:
    if shutil.which("qemu-system-i386", path=environment.get("PATH")):
        return
    if os.name != "nt":
        raise VerificationError("qemu-system-i386 is required")
    candidates = [
        pathlib.Path(r"C:\tmp\qemu-portable\qemu-system-i386.exe"),
        pathlib.Path(r"C:\Program Files\qemu\qemu-system-i386.exe"),
        pathlib.Path(r"C:\msys64\mingw64\bin\qemu-system-i386.exe"),
    ]
    qemu = next((candidate for candidate in candidates if candidate.is_file()), None)
    if qemu is None:
        raise VerificationError("qemu-system-i386 is required")
    environment["PATH"] = f"{qemu.parent}{os.pathsep}{environment.get('PATH', '')}"


def windows_codex_helper_directory(executable: pathlib.Path) -> pathlib.Path | None:
    """Return the helper directory only for a complete, matching Codex bundle."""
    executable = executable.resolve()
    adjacent = executable.parent
    if all((adjacent / helper).is_file() for helper in WINDOWS_CODEX_HELPERS):
        return adjacent

    package_root = executable.parent.parent
    manifest_path = package_root / "codex-package.json"
    if not manifest_path.is_file():
        return None
    try:
        manifest = json.loads(manifest_path.read_text("utf-8"))
        entrypoint = (package_root / manifest["entrypoint"]).resolve()
        resources = (package_root / manifest["resourcesDir"]).resolve()
    except (KeyError, OSError, ValueError):
        return None
    if entrypoint != executable:
        return None
    if all((resources / helper).is_file() for helper in WINDOWS_CODEX_HELPERS):
        return resources
    return None


def windows_path_candidates(
    command: str, environment: dict[str, str]
) -> list[pathlib.Path]:
    command_path = pathlib.Path(command)
    if command_path.parent != pathlib.Path("."):
        return [command_path] if command_path.is_file() else []

    suffixes = [""] if command_path.suffix else [
        suffix
        for extension in environment.get("PATHEXT", ".COM;.EXE;.BAT;.CMD").split(";")
        if (suffix := extension.strip())
    ]
    candidates: list[pathlib.Path] = []
    seen: set[str] = set()
    for raw_directory in environment.get("PATH", "").split(";"):
        directory = raw_directory.strip().strip('"')
        if not directory:
            continue
        for suffix in suffixes:
            candidate = pathlib.Path(directory) / f"{command}{suffix}"
            if not candidate.is_file():
                candidate = pathlib.Path(directory) / f"{command}{suffix.lower()}"
            if not candidate.is_file():
                continue
            key = str(candidate.resolve()).casefold()
            if key not in seen:
                seen.add(key)
                candidates.append(candidate.resolve())
    return candidates


def installed_windows_codex_candidates(
    environment: dict[str, str]
) -> list[pathlib.Path]:
    profile = pathlib.Path(environment.get("USERPROFILE", pathlib.Path.home()))
    codex_home = pathlib.Path(environment.get("CODEX_HOME", profile / ".codex"))
    local_app_data = pathlib.Path(
        environment.get("LOCALAPPDATA", profile / "AppData/Local")
    )
    searches = (
        (codex_home / "packages/standalone/releases", "*/bin/codex.exe"),
        (local_app_data / "OpenAI/Codex/bin", "*/codex.exe"),
        (
            profile / ".vscode/extensions",
            "openai.chatgpt-*/bin/windows-x86_64/codex.exe",
        ),
    )
    candidates: list[pathlib.Path] = []
    for root, pattern in searches:
        if not root.is_dir():
            continue
        matches = sorted(
            root.glob(pattern),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
        candidates.extend(path.resolve() for path in matches if path.is_file())
    return candidates


def resolve_codex(
    command: str,
    environment: dict[str, str],
    *,
    windows: bool | None = None,
) -> str:
    is_windows = os.name == "nt" if windows is None else windows
    if not is_windows:
        executable = shutil.which(command, path=environment.get("PATH"))
        if executable is None:
            raise VerificationError(f"Codex executable {command!r} was not found")
        return executable

    explicit = pathlib.Path(command).parent != pathlib.Path(".")
    candidates = windows_path_candidates(command, environment)
    if not explicit:
        candidates.extend(installed_windows_codex_candidates(environment))
    unique_candidates: list[pathlib.Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate).casefold()
        if key not in seen:
            seen.add(key)
            unique_candidates.append(candidate)

    for candidate in unique_candidates:
        if candidate.suffix.casefold() in {".cmd", ".bat"}:
            if explicit:
                return str(candidate)
            continue
        if candidate.suffix.casefold() == ".exe" and windows_codex_helper_directory(
            candidate
        ):
            return str(candidate)

    checked = ", ".join(str(path) for path in unique_candidates) or "none"
    helpers = ", ".join(WINDOWS_CODEX_HELPERS)
    raise VerificationError(
        f"no complete Windows Codex bundle was found; required helpers: {helpers}; "
        f"checked: {checked}"
    )


def prepare_codex_environment(
    executable: str,
    environment: dict[str, str],
    *,
    windows: bool | None = None,
) -> None:
    is_windows = os.name == "nt" if windows is None else windows
    if not is_windows or pathlib.Path(executable).suffix.casefold() != ".exe":
        return
    helper_directory = windows_codex_helper_directory(pathlib.Path(executable))
    if helper_directory is None:
        raise VerificationError("the selected Windows Codex bundle is incomplete")
    entries = [str(pathlib.Path(executable).parent), str(helper_directory)]
    current_path = environment.get("PATH", "")
    if current_path:
        entries.append(current_path)
    environment["PATH"] = ";".join(entries)


def tail(path: pathlib.Path, lines: int = 40) -> str:
    content = path.read_text("utf-8", errors="replace").splitlines()
    return "\n".join(content[-lines:])


@contextlib.contextmanager
def isolated_checkout(
    repo: pathlib.Path,
    baseline: str,
    evidence_destination: pathlib.Path | None = None,
):
    temporary_root = repo / "build/codex-worktrees"
    temporary_root.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.TemporaryDirectory(
        prefix="reist-codex-", dir=temporary_root
    )
    checkout = pathlib.Path(temporary.name) / "checkout"
    try:
        clone = subprocess.run(
            [
                "git",
                "clone",
                "--quiet",
                "--shared",
                "--no-checkout",
                str(repo),
                str(checkout),
            ],
            cwd=temporary.name,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if clone.returncode != 0:
            raise VerificationError(
                f"isolated clone failed: {clone.stdout.strip()} {clone.stderr.strip()}"
            )
        git(checkout, "remote", "remove", "origin")
        git(checkout, "config", "--local", "commit.gpgsign", "false")
        git(checkout, "config", "--local", "core.autocrlf", "false")
        git(checkout, "config", "--local", "core.eol", "lf")
        git(checkout, "config", "--local", "core.safecrlf", "false")
        git(checkout, "checkout", "--quiet", "--detach", baseline)
        for key in ("user.name", "user.email"):
            identity = subprocess.run(
                ["git", "config", "--get", key],
                cwd=repo,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            if identity.returncode == 0 and identity.stdout.strip():
                git(checkout, "config", key, identity.stdout.strip())
        yield checkout
    finally:
        evidence_source = checkout / "build/codex-agent"
        if evidence_destination is not None and evidence_source.is_dir():
            shutil.copytree(
                evidence_source,
                evidence_destination,
                dirs_exist_ok=True,
            )
        temporary.cleanup()


@contextlib.contextmanager
def in_place_checkout(
    repo: pathlib.Path,
    baseline: str,
    evidence_destination: pathlib.Path | None = None,
):
    """Expose package edits directly in the user's checked-out worktree."""
    del evidence_destination
    assert_clean(repo)
    if git(repo, "rev-parse", "HEAD") != baseline:
        raise VerificationError("main HEAD changed before in-place package run")
    yield repo


def prepare_agent_environment(
    worktree: pathlib.Path, environment: dict[str, str]
) -> dict[str, str]:
    agent_environment = environment.copy()
    temporary = worktree / ".reist-agent-tmp"
    temporary.mkdir(exist_ok=True)
    exclude = worktree / ".git/info/exclude"
    current_excludes = exclude.read_text("utf-8", errors="replace")
    if "/.reist-agent-tmp/" not in current_excludes.splitlines():
        exclude.write_text(current_excludes + "/.reist-agent-tmp/\n", "utf-8")
    for name in ("TEMP", "TMP", "TMPDIR"):
        agent_environment[name] = str(temporary)
    agent_environment["PYTHONDONTWRITEBYTECODE"] = "1"
    return agent_environment


def execute(args: argparse.Namespace) -> int:
    repo = pathlib.Path(args.repo).resolve()
    task_path = repo / "automation/reist-s03b.toml"
    schema_path = repo / ".codex/schemas/reist-run-result.schema.json"
    task = read_task(task_path)
    package = active_package(task)
    if args.dry_run:
        active_id = package["id"] if package else ""
        print(
            f"Ready: model=gpt-5.6-sol effort=low active={active_id} "
            f"max={args.max_packages} timeout={args.package_timeout_seconds}s"
        )
        return 0

    assert_clean(repo)
    environment = os.environ.copy()
    resolve_qemu(environment)
    codex = resolve_codex(args.codex, environment)
    prepare_codex_environment(codex, environment)
    print(f"Codex bundle: {codex}")
    log_root = repo / "build/codex-agent"
    log_root.mkdir(parents=True, exist_ok=True)

    for iteration in range(1, args.max_packages + 1):
        assert_clean(repo)
        before_head = git(repo, "rev-parse", "HEAD")
        before_task = read_task(task_path)
        package = active_package(before_task)
        if package is None:
            print("no_work: no active REIST package")
            return 0
        stamp = time.strftime("%Y%m%d-%H%M%S")
        event_log = log_root / f"{stamp}-events.jsonl"
        result_file = log_root / f"{stamp}-result.json"
        evidence_dir = log_root / f"{stamp}-{package['id']}-evidence"
        print(
            f"[{iteration}/{args.max_packages}] {package['id']} at {before_head[:12]}"
        )
        try:
            with in_place_checkout(repo, before_head, evidence_dir) as worktree:
                agent_environment = prepare_agent_environment(worktree, environment)
                isolated_task_path = worktree / "automation/reist-s03b.toml"
                isolated_schema_path = (
                    worktree / ".codex/schemas/reist-run-result.schema.json"
                )
                isolated_log_root = worktree / "build/codex-agent"
                isolated_log_root.mkdir(parents=True, exist_ok=True)
                active_file = isolated_log_root / f"{stamp}-active-package.json"
                isolated_result_file = isolated_log_root / f"{stamp}-result.json"
                active_file.write_text(
                    json.dumps(package, ensure_ascii=False, indent=2) + "\n",
                    "utf-8",
                )
                prompt = f"""Execute exactly the package contract in {active_file}.
Follow AGENTS.md and every invariant/stop condition. Do not implement the next
package. Use no subagents or reviewers.
Do not run the listed acceptance gates inside this nested agent sandbox; the
outer verifier runs every gate exactly once after validating your candidate
commit. You may run bounded lightweight inspections that do not duplicate a
listed gate. On success leave commit/passed/evidence/blocker empty; the outer
runner owns commit and gate bookkeeping. Update only this package's status and
the next active queue entry in automation/reist-s03b.toml, then return candidate
without staging or committing. On a blocker, do not restore files: return blocked
immediately without committing; this isolated checkout is discarded by the
runner. Return only the required result object with the full 40-character
commit SHA.
"""
                command = [
                    codex,
                    "exec",
                    "--strict-config",
                    "--cd",
                    str(worktree),
                    "--model",
                    "gpt-5.6-sol",
                    "--config",
                    'approval_policy="never"',
                    "--config",
                    'default_permissions=":workspace"',
                    "--config",
                    'model_reasoning_effort="low"',
                    "--config",
                    'model_verbosity="low"',
                    "--config",
                    "agents.max_concurrent_threads_per_session=1",
                    "--config",
                    "agents.enabled=false",
                    "--config",
                    'agents.default_subagent_model="gpt-5.6-sol"',
                    "--config",
                    'agents.default_subagent_reasoning_effort="low"',
                    "--ephemeral",
                    "--json",
                    "--output-schema",
                    str(isolated_schema_path),
                    "--output-last-message",
                    str(isolated_result_file),
                    "-",
                ]
                exit_code = run_bounded(
                    command,
                    worktree,
                    prompt,
                    event_log,
                    args.package_timeout_seconds,
                    agent_environment,
                )
                if isolated_result_file.is_file():
                    copy_result_file(isolated_result_file, result_file)
                if exit_code != 0 or not isolated_result_file.is_file():
                    raise VerificationError(
                        f"agent failed exit={exit_code}; log={event_log}\n"
                        f"{tail(event_log)}"
                    )
                result = json.loads(isolated_result_file.read_text("utf-8"))
                if (
                    result.get("status") in {"blocked", "candidate"}
                    and result.get("commit") == before_head
                ):
                    result["commit"] = ""
                    if result.get("status") == "blocked":
                        result["passed"] = []
                if result.get("status") == "candidate":
                    materialize_candidate(
                        worktree, before_head, before_task, package,
                        isolated_task_path, result
                    )
                    isolated_result_file.write_text(
                        json.dumps(result, ensure_ascii=False) + "\n", "utf-8"
                    )
                    copy_result_file(isolated_result_file, result_file)
                discarded_status = (
                    git(worktree, "status", "--short")
                    if result.get("status") == "blocked"
                    else ""
                )
                verified_head = verify_result(
                    worktree,
                    before_head,
                    before_task,
                    result,
                    isolated_task_path,
                    allow_dirty_blocked=True,
                )
                if result["status"] == "committed":
                    try:
                        passed = run_sandboxed_gates(
                            codex,
                            package,
                            worktree,
                            isolated_log_root,
                            args.package_timeout_seconds,
                            agent_environment,
                        )
                    except GateFailure as first_failure:
                        repair_log = isolated_log_root / f"{stamp}-repair-events.jsonl"
                        repair_result_file = isolated_log_root / f"{stamp}-repair-result.json"
                        repair_command = command.copy()
                        output_index = repair_command.index("--output-last-message") + 1
                        repair_command[output_index] = str(repair_result_file)
                        repair_prompt = f"""Repair only the first failing gate for package
{package['id']}: {first_failure.gate}

Failure log tail:
{tail(first_failure.log_path)}

Keep the verified task transition unchanged. Stay within allowed_files, use no
subagents, and make the smallest focused repair. Do not stage or commit. Return
candidate with empty commit, passed and blocker. If the failure cannot be fixed
within scope, return blocked with one concrete reason.
"""
                        repair_exit = run_bounded(
                            repair_command,
                            worktree,
                            repair_prompt,
                            repair_log,
                            min(args.package_timeout_seconds, 300),
                            agent_environment,
                        )
                        if repair_exit != 0 or not repair_result_file.is_file():
                            raise first_failure
                        repair_result = json.loads(
                            repair_result_file.read_text("utf-8")
                        )
                        if repair_result.get("status") != "candidate":
                            raise GateFailure(
                                first_failure.gate, repair_log, repair_exit or 2
                            )
                        verified_head = amend_repair_candidate(
                            worktree,
                            verified_head,
                            package,
                            isolated_task_path,
                            repair_result,
                        )
                        result.update(repair_result)
                        isolated_result_file.write_text(
                            json.dumps(result, ensure_ascii=False) + "\n", "utf-8"
                        )
                        copy_result_file(isolated_result_file, result_file)
                        verified_head = verify_result(
                            worktree,
                            before_head,
                            before_task,
                            result,
                            isolated_task_path,
                        )
                        passed = run_sandboxed_gates(
                            codex,
                            package,
                            worktree,
                            isolated_log_root,
                            args.package_timeout_seconds,
                            agent_environment,
                        )
                    if passed != result["passed"]:
                        raise VerificationError(
                            "outer gate order differs from committed evidence"
                        )
                    assert_clean(worktree)
                if result["status"] == "committed":
                    if git(repo, "rev-parse", "HEAD") != verified_head:
                        raise VerificationError(
                            "verified in-place package is not main HEAD"
                        )
                    assert_clean(repo)
        except PackageTimeout as error:
            print(f"blocked: {error}; in-place edits retained; log={event_log}")
            return 124
        except GateFailure as error:
            print(f"blocked: {error}; in-place candidate retained")
            return 2
        except (json.JSONDecodeError, VerificationError) as error:
            print(f"verification failed: {error}; log={event_log}")
            return 1

        print(f"{result['status']}: {result['summary']}")
        if result["status"] == "blocked":
            if discarded_status:
                print("retained in-place edits:")
                for line in discarded_status.splitlines():
                    print(f"  {line}")
            print(f"blocker: {result['blocker']}")
            return 2
        if result["status"] == "no_work":
            return 0
        print(f"committed {verified_head}; passed: {', '.join(result['passed'])}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo", default=pathlib.Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--codex", default="codex")
    parser.add_argument("--max-packages", type=int, choices=range(1, 7), default=6)
    parser.add_argument("--package-timeout-seconds", type=int, default=600)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.package_timeout_seconds <= 0:
        parser.error("--package-timeout-seconds must be positive")
    try:
        return execute(args)
    except VerificationError as error:
        print(f"orchestration error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
