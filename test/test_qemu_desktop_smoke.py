"""Host regressions for the framebuffer desktop QEMU smoke runner."""

from __future__ import annotations

import importlib.util
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run_qemu_desktop_smoke.py"


class DesktopSmokeRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.image = self.directory / "framebuffer image.img"
        self.image.write_bytes(b"fake image")
        self.arguments_file = self.directory / "arguments.txt"
        self.log_file = self.directory / "desktop.log"
        self.qemu = self._write_fake_qemu()

    def _write_fake_qemu(self) -> Path:
        if os.name == "nt":
            path = self.directory / "fake-qemu.cmd"
            path.write_text(
                "@echo off\n"
                "> \"%FAKE_QEMU_ARGS%\" echo %*\n"
                "if \"%FAKE_QEMU_MODE%\"==\"success\" goto success\n"
                "if \"%FAKE_QEMU_MODE%\"==\"not-exact\" goto not_exact\n"
                "if \"%FAKE_QEMU_MODE%\"==\"missing-fb\" goto missing_fb\n"
                "if \"%FAKE_QEMU_MODE%\"==\"reverse\" goto reverse\n"
                "if \"%FAKE_QEMU_MODE%\"==\"panic\" goto panic\n"
                "if \"%FAKE_QEMU_MODE%\"==\"early-exit\" goto early_exit\n"
                "if \"%FAKE_QEMU_MODE%\"==\"timeout\" goto timeout\n"
                "exit /b 99\n"
                ":success\n"
                "echo Framebuffer initialized: 1024x768x32 at 0x10000000\n"
                "echo BOOT_OK\n"
                "echo DESKTOP_OK\n"
                ":success_hold\n"
                "goto success_hold\n"
                ":not_exact\n"
                "echo Framebuffer initialized: 1024x768x32\n"
                "echo BOOT_OK\n"
                "echo NOT_DESKTOP_OK\n"
                "exit /b 0\n"
                ":missing_fb\n"
                "echo BOOT_OK\n"
                "echo DESKTOP_OK\n"
                "exit /b 0\n"
                ":reverse\n"
                "echo Framebuffer initialized: 1024x768x32\n"
                "echo DESKTOP_OK\n"
                "echo BOOT_OK\n"
                "exit /b 0\n"
                ":panic\n"
                "echo Framebuffer initialized: 1024x768x32\n"
                "echo BOOT_OK\n"
                "echo PANIC: desktop failed\n"
                "exit /b 0\n"
                ":early_exit\n"
                "echo Framebuffer initialized: 1024x768x32\n"
                "echo BOOT_OK\n"
                "echo DESKTOP_OK\n"
                "exit /b 0\n"
                ":timeout\n"
                "echo Framebuffer initialized: 1024x768x32\n"
                ":spin\n"
                "goto spin\n",
                encoding="ascii",
            )
            return path

        path = self.directory / "fake-qemu"
        path.write_text(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > \"$FAKE_QEMU_ARGS\"\n"
            "case \"$FAKE_QEMU_MODE\" in\n"
            " success) printf 'Framebuffer initialized: 1024x768x32 at 0x10000000\\nBOOT_OK\\nDESKTOP_OK\\n'; while :; do :; done ;;\n"
            " not-exact) printf 'Framebuffer initialized: 1024x768x32\\nBOOT_OK\\nNOT_DESKTOP_OK\\n' ;;\n"
            " missing-fb) printf 'BOOT_OK\\nDESKTOP_OK\\n' ;;\n"
            " reverse) printf 'Framebuffer initialized: 1024x768x32\\nDESKTOP_OK\\nBOOT_OK\\n' ;;\n"
            " panic) printf 'Framebuffer initialized: 1024x768x32\\nBOOT_OK\\nPANIC: desktop failed\\n' ;;\n"
            " early-exit) printf 'Framebuffer initialized: 1024x768x32\\nBOOT_OK\\nDESKTOP_OK\\n'; exit 0 ;;\n"
            " timeout) printf 'Framebuffer initialized: 1024x768x32\\n'; while :; do :; done ;;\n"
            " *) exit 99 ;;\n"
            "esac\n",
            encoding="ascii",
        )
        path.chmod(0o755)
        return path

    def run_smoke(self, mode: str, *, timeout: str = "3",
                  image: Path | None = None) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["FAKE_QEMU_MODE"] = mode
        environment["FAKE_QEMU_ARGS"] = str(self.arguments_file)
        return subprocess.run(
            [
                sys.executable, str(RUNNER),
                "--qemu", str(self.qemu),
                "--image", str(image or self.image),
                "--timeout", timeout,
                "--log", str(self.log_file),
            ],
            cwd=ROOT,
            env=environment,
            text=True,
            capture_output=True,
            timeout=8,
        )

    @staticmethod
    def output(result: subprocess.CompletedProcess[str]) -> str:
        return result.stdout + result.stderr

    def test_success_requires_framebuffer_boot_and_exact_desktop_lines(self) -> None:
        result = self.run_smoke("success")
        self.assertEqual(result.returncode, 0, self.output(result))
        transcript = self.log_file.read_text(encoding="utf-8")
        self.assertLess(transcript.index("Framebuffer initialized:"),
                        transcript.index("BOOT_OK"))
        self.assertLess(transcript.index("BOOT_OK"),
                        transcript.index("DESKTOP_OK"))

    def test_desktop_substring_is_not_accepted(self) -> None:
        result = self.run_smoke("not-exact")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NOT_DESKTOP_OK", self.output(result))

    def test_missing_framebuffer_and_wrong_order_fail(self) -> None:
        for mode in ("missing-fb", "reverse"):
            with self.subTest(mode=mode):
                result = self.run_smoke(mode)
                self.assertNotEqual(result.returncode, 0)

    def test_guest_failure_marker_fails_immediately(self) -> None:
        result = self.run_smoke("panic")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("PANIC: desktop failed", self.output(result))

    def test_desktop_must_survive_the_post_marker_stability_window(self) -> None:
        result = self.run_smoke("early-exit")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("stability window", self.output(result))

    def test_timeout_terminates_fake_qemu_and_preserves_transcript(self) -> None:
        started = time.monotonic()
        result = self.run_smoke("timeout", timeout="0.2")
        self.assertNotEqual(result.returncode, 0)
        self.assertLess(time.monotonic() - started, 3.0)
        self.assertIn("Framebuffer initialized:", self.output(result))

    def test_qemu_is_headless_but_keeps_the_standard_vga_device(self) -> None:
        result = self.run_smoke("success")
        self.assertEqual(result.returncode, 0, self.output(result))
        arguments = self.arguments_file.read_text(encoding="utf-8")
        self.assertIn("-display", arguments)
        self.assertIn("none", arguments)
        self.assertIn("-vga", arguments)
        self.assertIn("std", arguments)
        self.assertIn("-serial", arguments)
        self.assertIn("stdio", arguments)
        self.assertIn("-snapshot", arguments)
        self.assertIn(f"file={self.image}", arguments)

    def test_missing_image_is_rejected_before_qemu_starts(self) -> None:
        result = self.run_smoke("success", image=self.directory / "missing.img")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.arguments_file.exists())


class DesktopSmokeQmpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        spec = importlib.util.spec_from_file_location("desktop_smoke", RUNNER)
        assert spec is not None and spec.loader is not None
        cls.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.runner)

    def test_qmp_screendump_is_requested_after_capability_negotiation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            screenshot = Path(temporary) / "desktop.ppm"
            listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            port = int(listener.getsockname()[1])
            requests: list[dict[str, object]] = []
            errors: list[BaseException] = []

            def fake_qmp() -> None:
                try:
                    connection, _ = listener.accept()
                    with connection, connection.makefile("rwb") as stream:
                        stream.write(b'{"QMP":{"version":{},"capabilities":[]}}\r\n')
                        stream.flush()
                        for _ in range(2):
                            request = json.loads(stream.readline())
                            requests.append(request)
                            if request["execute"] == "screendump":
                                Path(request["arguments"]["filename"]).write_bytes(
                                    b"P6\n1 1\n255\n\0\0\0"
                                )
                            stream.write((json.dumps({
                                "return": {}, "id": request["id"],
                            }) + "\r\n").encode("utf-8"))
                            stream.flush()
                except BaseException as error:  # surfaced in the test thread
                    errors.append(error)
                finally:
                    listener.close()

            server = threading.Thread(target=fake_qmp, daemon=True)
            server.start()
            error = self.runner.qmp_screendump(
                port, screenshot, time.monotonic() + 2
            )
            server.join(timeout=2)

            self.assertIsNone(error)
            self.assertFalse(errors)
            self.assertEqual(requests[0]["execute"], "qmp_capabilities")
            self.assertEqual(requests[1]["execute"], "screendump")
            self.assertGreater(screenshot.stat().st_size, 0)

    def test_unreaped_process_is_a_cleanup_error(self) -> None:
        class UnreapableProcess:
            pid = 4242
            returncode = None

            def __init__(self) -> None:
                self.killed = False

            def poll(self):
                return None

            def terminate(self) -> None:
                pass

            def kill(self) -> None:
                self.killed = True

            def wait(self, timeout=None):
                raise subprocess.TimeoutExpired("fake-qemu", timeout)

        process = UnreapableProcess()
        error = self.runner.stop_process(process)
        self.assertTrue(process.killed)
        self.assertIn("was not reaped", error)

        source = RUNNER.read_text(encoding="utf-8")
        self.assertIn("cleanup_error = stop_process(process)", source)
        self.assertIn("if cleanup_error is not None:", source)


class DesktopSmokePackagingTests(unittest.TestCase):
    def test_make_builds_framebuffer_image_and_runs_desktop_smoke(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("test-smoke-desktop", makefile)
        body = makefile[makefile.index("test-smoke-desktop:\n") :]
        body = body[: body.index("\n\n")]
        self.assertIn("native-image TARGET=qemu VIDEO=framebuffer", body)
        self.assertIn("scripts/run_qemu_desktop_smoke.py", body)
        self.assertIn("guest-smoke-desktop.log", body)
        self.assertIn("--screenshot", body)

    def test_ci_runs_desktop_smoke_and_uploads_its_artifacts(self) -> None:
        workflow = (ROOT / ".github/workflows/build.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("make test-smoke-desktop TARGET=qemu", workflow)
        self.assertIn("build/guest-smoke-desktop.log", workflow)
        self.assertIn("build/guest-smoke-desktop.ppm", workflow)


if __name__ == "__main__":
    unittest.main()
