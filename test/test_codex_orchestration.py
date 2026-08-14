import contextlib
import importlib.util
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
import types
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER_PATH = ROOT / "scripts/run_reist_autonomous.py"
RUNNER_SPEC = importlib.util.spec_from_file_location("reist_runner", RUNNER_PATH)
RUNNER = importlib.util.module_from_spec(RUNNER_SPEC)
assert RUNNER_SPEC.loader is not None
RUNNER_SPEC.loader.exec_module(RUNNER)


class CodexOrchestrationTests(unittest.TestCase):
    def test_windows_codex_resolution_skips_incomplete_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            incomplete = root / "incomplete"
            complete = root / "complete"
            incomplete.mkdir()
            complete.mkdir()
            (incomplete / "codex.exe").write_bytes(b"")
            (complete / "codex.exe").write_bytes(b"")
            for helper in RUNNER.WINDOWS_CODEX_HELPERS:
                (complete / helper).write_bytes(b"")
            environment = {
                "PATH": f"{incomplete};{complete}",
                "PATHEXT": ".EXE;.CMD",
                "USERPROFILE": str(root / "profile"),
                "LOCALAPPDATA": str(root / "local"),
            }
            resolved = RUNNER.resolve_codex("codex", environment, windows=True)
            self.assertEqual(pathlib.Path(resolved), complete / "codex.exe")
            RUNNER.prepare_codex_environment(resolved, environment, windows=True)
            path_entries = environment["PATH"].split(";")
            self.assertEqual(pathlib.Path(path_entries[0]), complete)
            self.assertEqual(pathlib.Path(path_entries[1]), complete)

    def test_windows_codex_resolution_fails_closed_without_helpers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            incomplete = root / "incomplete"
            incomplete.mkdir()
            executable = incomplete / "codex.exe"
            executable.write_bytes(b"")
            environment = {
                "PATH": str(incomplete),
                "PATHEXT": ".EXE",
                "USERPROFILE": str(root / "profile"),
                "LOCALAPPDATA": str(root / "local"),
            }
            with self.assertRaisesRegex(
                RUNNER.VerificationError, "no complete Windows Codex bundle"
            ):
                RUNNER.resolve_codex("codex", environment, windows=True)
            with self.assertRaisesRegex(
                RUNNER.VerificationError, "no complete Windows Codex bundle"
            ):
                RUNNER.resolve_codex(str(executable), environment, windows=True)

    def test_windows_codex_resolution_accepts_packaged_resources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary) / "release"
            executable = root / "bin/codex.exe"
            resources = root / "codex-resources"
            executable.parent.mkdir(parents=True)
            resources.mkdir()
            executable.write_bytes(b"")
            for helper in RUNNER.WINDOWS_CODEX_HELPERS:
                (resources / helper).write_bytes(b"")
            (root / "codex-package.json").write_text(
                json.dumps(
                    {
                        "entrypoint": "bin/codex.exe",
                        "resourcesDir": "codex-resources",
                    }
                ),
                "utf-8",
            )
            resolved = RUNNER.resolve_codex(
                str(executable), {"PATH": ""}, windows=True
            )
            self.assertEqual(pathlib.Path(resolved), executable)

    def test_windows_codex_resolution_finds_managed_package_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = pathlib.Path(temporary) / "profile"
            root = profile / ".codex/packages/standalone/releases/0.147.0"
            executable = root / "bin/codex.exe"
            resources = root / "codex-resources"
            executable.parent.mkdir(parents=True)
            resources.mkdir()
            executable.write_bytes(b"")
            for helper in RUNNER.WINDOWS_CODEX_HELPERS:
                (resources / helper).write_bytes(b"")
            (root / "codex-package.json").write_text(
                json.dumps(
                    {
                        "entrypoint": "bin/codex.exe",
                        "resourcesDir": "codex-resources",
                    }
                ),
                "utf-8",
            )
            environment = {
                "PATH": "",
                "PATHEXT": ".EXE",
                "USERPROFILE": str(profile),
                "LOCALAPPDATA": str(profile / "local"),
            }
            resolved = RUNNER.resolve_codex("codex", environment, windows=True)
            self.assertEqual(pathlib.Path(resolved), executable)

    def test_project_config_pins_sol_light_and_bounded_agents(self):
        config = tomllib.loads((ROOT / ".codex/config.toml").read_text("utf-8"))
        self.assertEqual(config["model"], "gpt-5.6-sol")
        self.assertEqual(config["model_reasoning_effort"], "low")
        self.assertEqual(config["model_verbosity"], "low")
        self.assertEqual(config["approval_policy"], "never")
        self.assertEqual(config["default_permissions"], ":workspace")
        self.assertNotIn("sandbox_mode", config)
        self.assertEqual(config["agents"]["max_concurrent_threads_per_session"], 1)
        reviewer = tomllib.loads(
            (ROOT / ".codex/agents/reist-reviewer.toml").read_text("utf-8")
        )
        self.assertEqual(reviewer["model"], "gpt-5.6-sol")
        self.assertEqual(reviewer["default_permissions"], ":read-only")
        self.assertNotIn("sandbox_mode", reviewer)

    def test_task_queue_has_exactly_one_active_bounded_package(self):
        task = tomllib.loads(
            (ROOT / "automation/reist-s03b.toml").read_text("utf-8")
        )
        packages = task["packages"]
        active = [package for package in packages if package["status"] == "active"]
        if task["active_id"]:
            self.assertEqual(len(active), 1)
            self.assertEqual(task["active_id"], active[0]["id"])
        else:
            self.assertEqual(active, [])
            self.assertTrue(all(package["status"] == "done" for package in packages))
        self.assertEqual(len({package["id"] for package in packages}), len(packages))
        for package in packages:
            self.assertTrue(package["allowed_files"])
            self.assertTrue(package["invariants"])
            self.assertTrue(package["targeted_tests"])
            self.assertTrue(package["package_tests"])
            self.assertTrue(package["runtime_tests"])
            self.assertTrue(package["stop_conditions"])
            self.assertRegex(package["commit_message"], r"^(feat|fix|test|docs): ")

    def test_result_schema_is_closed_and_machine_readable(self):
        schema = json.loads(
            (ROOT / ".codex/schemas/reist-run-result.schema.json").read_text(
                "utf-8"
            )
        )
        self.assertFalse(schema["additionalProperties"])
        self.assertEqual(
            schema["properties"]["status"]["enum"],
            ["candidate", "blocked", "no_work"],
        )
        self.assertEqual(schema["properties"]["commit"]["maxLength"], 40)

    def test_runner_canonicalizes_gate_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            task = pathlib.Path(temporary) / "task.toml"
            task.write_text(
                'version = 1\n\n[[packages]]\nid = "A"\n'
                'evidence = ["wrong"]\n\n[[packages]]\nid = "B"\n'
                'evidence = []\n',
                "utf-8",
            )
            gates = ["python test/test_a.py -q", ".\\scripts\\gate.ps1"]
            RUNNER.canonicalize_task_evidence(task, "A", gates)
            parsed = tomllib.loads(task.read_text("utf-8"))
            self.assertEqual(parsed["packages"][0]["evidence"], gates)
            self.assertEqual(parsed["packages"][1]["evidence"], [])

    def test_runner_is_bounded_and_verifies_git_postconditions(self):
        runner = RUNNER_PATH.read_text("utf-8")
        wrapper = (ROOT / "scripts/run-reist-autonomous.ps1").read_text("utf-8")
        self.assertNotIn('"--approve-for-me"', runner)
        self.assertIn('approval_policy="never"', runner)
        self.assertIn('default_permissions=":workspace"', runner)
        self.assertIn('"sandbox",', runner)
        self.assertIn('"-P",', runner)
        self.assertIn(":workspace", runner)
        self.assertIn("--include-managed-config", runner)
        self.assertIn('"gpt-5.6-sol"', runner)
        self.assertIn('model_reasoning_effort="low"', runner)
        self.assertIn("agents.max_concurrent_threads_per_session=1", runner)
        self.assertIn("agents.enabled=false", runner)
        self.assertNotIn("agents.reist_reviewer.config_file", runner)
        self.assertIn("PackageTimeoutSeconds = 600", wrapper)
        self.assertNotIn("dangerously-bypass-approvals", runner)
        self.assertIn("run_bounded", runner)
        self.assertIn("taskkill", runner)
        self.assertIn('"rev-list"', runner)
        self.assertIn('"diff-tree"', runner)
        self.assertIn("expected_task_after_success", runner)
        self.assertIn("isolated_checkout", runner)
        self.assertIn('"clone",', runner)
        self.assertIn('"remote", "remove", "origin"', runner)
        self.assertIn('"merge", "--ff-only"', runner)
        self.assertIn("outer verifier runs every gate exactly once", runner)
        self.assertIn("canonicalize_task_evidence", runner)
        self.assertIn('result.get("commit") == before_head', runner)
        contract = (ROOT / "AGENTS.md").read_text("utf-8")
        self.assertIn("outer runner is the only gate authority", contract.lower())
        self.assertIn("Do not run the listed acceptance gates", contract)
        self.assertIn("prepare_agent_environment", runner)
        self.assertIn("run_reist_autonomous.py", wrapper)

    def test_gate_parser_rejects_shell_and_path_escape(self):
        command = RUNNER.trusted_gate_command(
            "python test/test_reist_ipc.py -q", ROOT, dict(os.environ)
        )
        self.assertEqual(pathlib.Path(command[0]), pathlib.Path(sys.executable))
        self.assertEqual(pathlib.Path(command[1]), ROOT / "test/test_reist_ipc.py")
        self.assertEqual(command[2:], ["-q"])
        for invalid in (
            "python ../evil.py -q",
            "python test/test_reist_ipc.py -q; whoami",
            "cmd /c whoami",
            ".\\scripts\\unknown.ps1",
        ):
            with self.subTest(gate=invalid):
                with self.assertRaises(RUNNER.VerificationError):
                    RUNNER.trusted_gate_command(invalid, ROOT, dict(os.environ))

    def test_outer_gates_are_sandboxed_once_and_stop_on_failure(self):
        package = {
            "targeted_tests": [
                "python test/test_reist_ipc.py -q",
                "python test/test_reist_critical_object.py -q",
            ],
            "package_tests": [],
            "runtime_tests": [],
        }
        calls = []
        original = RUNNER.run_bounded

        def fake_run(command, cwd, prompt, log_path, timeout_seconds, env=None):
            calls.append(command)
            log_path.write_text("gate\n", "utf-8")
            return 1 if len(calls) == 1 else 0

        RUNNER.run_bounded = fake_run
        try:
            with tempfile.TemporaryDirectory() as temporary:
                with self.assertRaises(RUNNER.GateFailure):
                    RUNNER.run_sandboxed_gates(
                        "codex",
                        package,
                        ROOT,
                        pathlib.Path(temporary),
                        30,
                        dict(os.environ),
                    )
            self.assertEqual(len(calls), 1)
            self.assertEqual(
                calls[0][1:7],
                [
                    "sandbox",
                    "-P",
                    ":workspace",
                    "--include-managed-config",
                    "-C",
                    str(ROOT),
                ],
            )
            self.assertEqual(pathlib.Path(calls[0][7]), pathlib.Path(sys.executable))
        finally:
            RUNNER.run_bounded = original

    def test_windows_runtime_gate_reuses_reference_build(self):
        runner = (ROOT / "scripts/test-reist-runtime.ps1").read_text("utf-8")
        self.assertIn("scripts\\run_qemu_smoke.py", runner)
        self.assertIn("build\\reist-os.img", runner)
        self.assertIn("'normal', 'pit', 'watchdog', 'memory'", runner)
        self.assertIn("catch {", runner)
        self.assertIn("Get-Content -LiteralPath $gateLog -Tail 40", runner)
        self.assertNotIn("make test-smoke", runner)
        task = tomllib.loads(
            (ROOT / "automation/reist-s03b.toml").read_text("utf-8")
        )
        for package in task["packages"]:
            self.assertTrue(
                all("test-reist-runtime.ps1" in gate for gate in package["runtime_tests"])
            )

    def test_package_gate_is_single_pass_and_log_compacted(self):
        gate = (ROOT / "scripts/test-reist-package.ps1").read_text("utf-8")
        self.assertIn("build-windows.ps1", gate)
        self.assertIn("-RunTests", gate)
        self.assertIn("*> $log", gate)
        self.assertIn("catch {", gate)
        self.assertIn("$artifactDeadline", gate)
        self.assertIn("build\\reist-os.img", gate)
        self.assertIn("Get-Content -LiteralPath $log -Tail 40", gate)
        task = tomllib.loads(
            (ROOT / "automation/reist-s03b.toml").read_text("utf-8")
        )
        for package in task["packages"]:
            self.assertEqual(len(package["package_tests"]), 1)
            self.assertIn("test-reist-package.ps1", package["package_tests"][0])

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 is required")
    def test_package_gate_prints_tail_when_build_script_throws(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            fake = root / "fake-build.ps1"
            fake.write_text(
                "param([string]$Target,[string]$Video,[switch]$RunTests)\n"
                "Write-Output 'fake-build-tail'\n"
                "throw 'fake-build-failure'\n",
                "utf-8",
            )
            result = subprocess.run(
                [
                    shutil.which("pwsh"),
                    "-NoProfile",
                    "-File",
                    str(ROOT / "scripts/test-reist-package.ps1"),
                    "-BuildScript",
                    str(fake),
                    "-LogRoot",
                    str(root / "logs"),
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("PACKAGE FAIL", result.stdout)
            self.assertIn("fake-build-tail", result.stdout)
            self.assertIn("fake-build-failure", result.stdout)

    def test_copilot_file_only_points_to_current_contracts(self):
        guidance = (ROOT / ".github/copilot-instructions.md").read_text("utf-8")
        self.assertIn("AGENTS.md", guidance)
        self.assertIn("reist-s03b.toml", guidance)
        self.assertNotIn("WSL2 required", guidance)
        self.assertNotIn("GRUB loads", guidance)


class AutonomousVerifierTests(unittest.TestCase):
    TASK_RELATIVE = pathlib.Path(".codex/tasks/task.toml")
    EXEC_TASK_RELATIVE = pathlib.Path("automation/reist-s03b.toml")
    GATES = ["target", "package", "runtime"]

    def git(self, repo, *arguments):
        result = subprocess.run(
            ["git", *arguments],
            cwd=repo,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )
        return result.stdout.strip()

    def write_task(self, repo, completed=False, mutate_scope=False, relative=None):
        relative = relative or self.TASK_RELATIVE
        task = repo / relative
        task.parent.mkdir(parents=True, exist_ok=True)
        active_id = "P2" if completed else "P1"
        p1_status = "done" if completed else "active"
        p2_status = "active" if completed else "queued"
        scope = "changed" if mutate_scope else "scope"
        evidence = '["target", "package", "runtime"]' if completed else "[]"
        task.write_text(
            f'''version = 1
program = "test"
active_id = "{active_id}"

[[packages]]
id = "P1"
status = "{p1_status}"
title = "one"
scope = "{scope}"
allowed_files = ["allowed.txt", "{relative.as_posix()}"]
invariants = ["bounded"]
targeted_tests = ["target"]
package_tests = ["package"]
runtime_tests = ["runtime"]
stop_conditions = ["stop"]
commit_message = "feat: exact subject"
evidence = {evidence}

[[packages]]
id = "P2"
status = "{p2_status}"
title = "two"
scope = "scope"
allowed_files = ["next.txt", "{relative.as_posix()}"]
invariants = ["bounded"]
targeted_tests = ["target"]
package_tests = ["package"]
runtime_tests = ["runtime"]
stop_conditions = ["stop"]
commit_message = "feat: next"
evidence = []
''',
            "utf-8",
        )

    def make_repo(self):
        temporary = tempfile.TemporaryDirectory()
        repo = pathlib.Path(temporary.name)
        self.git(repo, "init", "-q")
        self.git(repo, "config", "user.name", "REIST test")
        self.git(repo, "config", "user.email", "reist-test@example.invalid")
        (repo / "allowed.txt").write_text("base\n", "utf-8")
        self.write_task(repo)
        self.git(repo, "add", ".")
        self.git(repo, "commit", "-q", "-m", "test: baseline")
        before_head = self.git(repo, "rev-parse", "HEAD")
        before_task = RUNNER.read_task(repo / self.TASK_RELATIVE)
        return temporary, repo, before_head, before_task

    def result(self, repo, status="committed", subject_commit=True):
        commit = self.git(repo, "rev-parse", "HEAD") if subject_commit else ""
        return {
            "status": status,
            "package_id": "P1" if status != "no_work" else "",
            "summary": "result",
            "commit": commit,
            "passed": self.GATES if status == "committed" else [],
            "blocker": "blocked safely" if status == "blocked" else "",
        }

    def commit_success_shape(self, repo, subject="feat: exact subject", extra=False,
                             mutate_scope=False):
        (repo / "allowed.txt").write_text("changed\n", "utf-8")
        if extra:
            (repo / "foreign.txt").write_text("foreign\n", "utf-8")
        self.write_task(repo, completed=True, mutate_scope=mutate_scope)
        self.git(repo, "add", ".")
        self.git(repo, "commit", "-q", "-m", subject)

    def test_accepts_one_exact_scoped_commit(self):
        temporary, repo, head, task = self.make_repo()
        with temporary:
            self.commit_success_shape(repo)
            result = self.result(repo)
            verified = RUNNER.verify_result(
                repo, head, task, result, repo / self.TASK_RELATIVE
            )
            self.assertEqual(verified, result["commit"])

    def test_runner_materializes_scoped_candidate_commit(self):
        temporary, repo, head, task = self.make_repo()
        with temporary:
            (repo / "allowed.txt").write_text("changed\n", "utf-8")
            self.write_task(repo, completed=True)
            result = {
                "status": "candidate",
                "package_id": "P1",
                "summary": "candidate",
                "commit": "",
                "passed": [],
                "blocker": "",
            }
            commit = RUNNER.materialize_candidate(
                repo, head, task, RUNNER.active_package(task),
                repo / self.TASK_RELATIVE, result
            )
            self.assertEqual(result["status"], "committed")
            self.assertEqual(result["commit"], commit)
            self.assertEqual(result["passed"], self.GATES)
            self.assertEqual(self.git(repo, "show", "-s", "--format=%s"),
                             "feat: exact subject")
            self.assertEqual(self.git(repo, "status", "--porcelain"), "")

            (repo / "allowed.txt").write_text("repaired\n", "utf-8")
            repair = {
                "status": "candidate",
                "package_id": "P1",
                "summary": "repair",
                "commit": "",
                "passed": [],
                "blocker": "",
            }
            amended = RUNNER.amend_repair_candidate(
                repo, commit, RUNNER.active_package(task),
                repo / self.TASK_RELATIVE, repair
            )
            self.assertNotEqual(amended, commit)
            self.assertEqual(self.git(repo, "rev-list", "--count", f"{head}..HEAD"), "1")
            self.assertEqual(repair["passed"], self.GATES)

    def test_rejects_wrong_subject_scope_and_task_transition(self):
        cases = [
            ("fix: wrong subject", False, False),
            ("feat: exact subject", True, False),
            ("feat: exact subject", False, True),
        ]
        for subject, extra, mutate_scope in cases:
            with self.subTest(subject=subject, extra=extra, scope=mutate_scope):
                temporary, repo, head, task = self.make_repo()
                with temporary:
                    self.commit_success_shape(repo, subject, extra, mutate_scope)
                    with self.assertRaises(RUNNER.VerificationError):
                        RUNNER.verify_result(
                            repo, head, task, self.result(repo), repo / self.TASK_RELATIVE
                        )

    def test_rejects_two_commits_and_commit_under_blocked(self):
        temporary, repo, head, task = self.make_repo()
        with temporary:
            (repo / "allowed.txt").write_text("first\n", "utf-8")
            self.git(repo, "add", "allowed.txt")
            self.git(repo, "commit", "-q", "-m", "test: intermediate")
            self.write_task(repo, completed=True)
            self.git(repo, "add", ".codex/tasks/task.toml")
            self.git(repo, "commit", "-q", "-m", "feat: exact subject")
            with self.assertRaises(RUNNER.VerificationError):
                RUNNER.verify_result(
                    repo, head, task, self.result(repo), repo / self.TASK_RELATIVE
                )

        temporary, repo, head, task = self.make_repo()
        with temporary:
            (repo / "allowed.txt").write_text("unexpected\n", "utf-8")
            self.git(repo, "add", "allowed.txt")
            self.git(repo, "commit", "-q", "-m", "feat: exact subject")
            with self.assertRaises(RUNNER.VerificationError):
                RUNNER.verify_result(
                    repo,
                    head,
                    task,
                    self.result(repo, status="blocked", subject_commit=False),
                    repo / self.TASK_RELATIVE,
                    allow_dirty_blocked=True,
                )

    def test_blocked_result_discards_uncommitted_isolated_edits(self):
        temporary, repo, head, task = self.make_repo()
        with temporary:
            (repo / "allowed.txt").write_text("unfinished\n", "utf-8")
            with self.assertRaises(RUNNER.VerificationError):
                RUNNER.verify_result(
                    repo,
                    head,
                    task,
                    self.result(repo, status="blocked", subject_commit=False),
                    repo / self.TASK_RELATIVE,
                )
            verified = RUNNER.verify_result(
                repo,
                head,
                task,
                self.result(repo, status="blocked", subject_commit=False),
                repo / self.TASK_RELATIVE,
                allow_dirty_blocked=True,
            )
            self.assertEqual(verified, head)
            self.assertNotEqual(self.git(repo, "status", "--porcelain"), "")

    def test_isolated_checkout_uses_stable_git_and_workspace_temp(self):
        temporary, repo, head, _ = self.make_repo()
        with temporary:
            with RUNNER.isolated_checkout(repo, head) as checkout:
                expected_root = repo / "build/codex-worktrees"
                self.assertTrue(checkout.is_relative_to(expected_root))
                self.assertEqual(
                    self.git(checkout, "config", "core.autocrlf"), "false"
                )
                self.assertEqual(self.git(checkout, "config", "core.eol"), "lf")
                environment = RUNNER.prepare_agent_environment(
                    checkout, {"PATH": "test"}
                )
                for name in ("TEMP", "TMP", "TMPDIR"):
                    self.assertEqual(
                        pathlib.Path(environment[name]),
                        checkout / ".reist-agent-tmp",
                    )
                self.assertEqual(self.git(checkout, "status", "--porcelain"), "")

    def test_rejects_no_work_while_active(self):
        temporary, repo, head, task = self.make_repo()
        with temporary:
            with self.assertRaises(RUNNER.VerificationError):
                RUNNER.verify_result(
                    repo,
                    head,
                    task,
                    self.result(repo, status="no_work", subject_commit=False),
                    repo / self.TASK_RELATIVE,
                )

    def test_wall_clock_timeout_terminates_agent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            started = time.monotonic()
            with self.assertRaises(RUNNER.PackageTimeout):
                RUNNER.run_bounded(
                    [sys.executable, "-c", "import time; time.sleep(10)"],
                    root,
                    "",
                    root / "agent.log",
                    0.2,
                )
            self.assertLess(time.monotonic() - started, 5.0)

    def test_failed_fake_agent_cannot_mutate_main_or_advance_baseline(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = pathlib.Path(temporary)
            self.git(repo, "init", "-q")
            self.git(repo, "config", "user.name", "REIST test")
            self.git(repo, "config", "user.email", "reist-test@example.invalid")
            (repo / "allowed.txt").write_text("base\n", "utf-8")
            self.write_task(repo, relative=self.EXEC_TASK_RELATIVE)
            schema = repo / ".codex/schemas/reist-run-result.schema.json"
            schema.parent.mkdir(parents=True, exist_ok=True)
            schema.write_text('{"type":"object"}\n', "utf-8")
            (repo / "AGENTS.md").write_text("Test contract.\n", "utf-8")
            (repo / ".gitignore").write_text("build/\n", "utf-8")
            if sys.platform == "win32":
                fake = repo / "fake-codex.cmd"
                fake.write_text(
                    "@echo off\r\n"
                    "echo foreign>foreign.txt\r\n"
                    "git add foreign.txt\r\n"
                    "git commit -q -m \"feat: invalid scope\"\r\n"
                    "exit /b 1\r\n",
                    "utf-8",
                )
            else:
                fake = repo / "fake-codex"
                fake.write_text(
                    "#!/bin/sh\n"
                    "printf 'foreign\\n' > foreign.txt\n"
                    "git add foreign.txt\n"
                    "git commit -q -m 'feat: invalid scope'\n"
                    "exit 1\n",
                    "utf-8",
                )
                fake.chmod(0o755)

            self.git(repo, "add", ".")
            self.git(repo, "commit", "-q", "-m", "test: baseline")
            baseline = self.git(repo, "rev-parse", "HEAD")

            arguments = types.SimpleNamespace(
                repo=repo,
                codex=str(fake),
                max_packages=1,
                package_timeout_seconds=30,
                dry_run=False,
            )
            original_resolve_qemu = RUNNER.resolve_qemu
            original_run_bounded = RUNNER.run_bounded
            RUNNER.resolve_qemu = lambda environment: None
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    first_result = RUNNER.execute(arguments)
                self.assertEqual(first_result, 1)
                self.assertEqual(self.git(repo, "rev-parse", "HEAD"), baseline)
                self.assertEqual(self.git(repo, "status", "--porcelain"), "")
                with contextlib.redirect_stdout(io.StringIO()):
                    second_result = RUNNER.execute(arguments)
                self.assertEqual(second_result, 1)
                self.assertEqual(self.git(repo, "rev-parse", "HEAD"), baseline)
                self.assertEqual(self.git(repo, "status", "--porcelain"), "")

                def invalid_result_agent(command, cwd, prompt, log_path,
                                         timeout_seconds, env=None):
                    (cwd / "foreign.txt").write_text("foreign\n", "utf-8")
                    self.git(cwd, "add", "foreign.txt")
                    self.git(cwd, "commit", "-q", "-m", "feat: invalid scope")
                    output_index = command.index("--output-last-message") + 1
                    pathlib.Path(command[output_index]).write_text("{", "utf-8")
                    log_path.write_text("invalid result\n", "utf-8")
                    return 0

                RUNNER.run_bounded = invalid_result_agent
                with contextlib.redirect_stdout(io.StringIO()):
                    invalid_result = RUNNER.execute(arguments)
                self.assertEqual(invalid_result, 1)
                self.assertEqual(self.git(repo, "rev-parse", "HEAD"), baseline)
                self.assertEqual(self.git(repo, "status", "--porcelain"), "")
            finally:
                RUNNER.resolve_qemu = original_resolve_qemu
                RUNNER.run_bounded = original_run_bounded


if __name__ == "__main__":
    unittest.main()
