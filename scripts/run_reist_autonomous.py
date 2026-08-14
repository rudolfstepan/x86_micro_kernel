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
    required_gates = [
        *package["targeted_tests"],
        *package["package_tests"],
        *package["runtime_tests"],
    ]
    if passed != required_gates or after_completed["evidence"] != required_gates:
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
    return agent_environment


def execute(args: argparse.Namespace) -> int:
    repo = pathlib.Path(args.repo).resolve()
    task_path = repo / ".codex/tasks/reist-s03b.toml"
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
            with isolated_checkout(repo, before_head, evidence_dir) as worktree:
                agent_environment = prepare_agent_environment(worktree, environment)
                isolated_task_path = worktree / ".codex/tasks/reist-s03b.toml"
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
package. Use no parallel writers and at most one read-only reist_reviewer.
Run every listed gate. On success update only this package's status/evidence
and the next active queue entry in .codex/tasks/reist-s03b.toml, commit once
with the exact commit_message, and leave a clean worktree. On a blocker, do not
restore files: return blocked immediately without committing; this isolated
checkout is discarded by the runner. Run a failing gate at most twice total
(initial run plus one focused repair), then stop. Return only the required
result object with the full 40-character commit SHA.
"""
                command = [
                    codex,
                    "exec",
                    "--strict-config",
                    "--approve-for-me",
                    "--cd",
                    str(worktree),
                    "--model",
                    "gpt-5.6-sol",
                    "--config",
                    'model_reasoning_effort="low"',
                    "--config",
                    'model_verbosity="low"',
                    "--config",
                    "agents.max_concurrent_threads_per_session=1",
                    "--config",
                    'agents.default_subagent_model="gpt-5.6-sol"',
                    "--config",
                    'agents.default_subagent_reasoning_effort="low"',
                    "--config",
                    (
                        'agents.reist_reviewer.description="Read-only REIST '
                        'P0/P1 package reviewer"'
                    ),
                    "--config",
                    (
                        'agents.reist_reviewer.config_file="'
                        + str(
                            worktree / ".codex/agents/reist-reviewer.toml"
                        ).replace("\\", "/")
                        + '"'
                    ),
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
                    shutil.copyfile(isolated_result_file, result_file)
                if exit_code != 0 or not isolated_result_file.is_file():
                    raise VerificationError(
                        f"agent failed exit={exit_code}; log={event_log}\n"
                        f"{tail(event_log)}"
                    )
                result = json.loads(isolated_result_file.read_text("utf-8"))
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
                assert_clean(repo)
                if git(repo, "rev-parse", "HEAD") != before_head:
                    raise VerificationError(
                        "main HEAD changed during the isolated package run"
                    )
                if result["status"] == "committed":
                    git(
                        repo,
                        "fetch",
                        "--quiet",
                        "--no-tags",
                        str(worktree),
                        verified_head,
                    )
                    git(repo, "merge", "--ff-only", verified_head)
                    if git(repo, "rev-parse", "HEAD") != verified_head:
                        raise VerificationError(
                            "verified package did not fast-forward main HEAD"
                        )
                    assert_clean(repo)
        except PackageTimeout as error:
            print(f"blocked: {error}; main worktree unchanged; log={event_log}")
            return 124
        except (json.JSONDecodeError, VerificationError) as error:
            print(f"verification failed: {error}; log={event_log}")
            return 1

        print(f"{result['status']}: {result['summary']}")
        if result["status"] == "blocked":
            if discarded_status:
                discarded = ", ".join(
                    line[3:] for line in discarded_status.splitlines()
                )
                print(f"discarded isolated edits: {discarded}")
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
    parser.add_argument("--package-timeout-seconds", type=int, default=7200)
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
