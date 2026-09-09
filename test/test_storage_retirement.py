"""Actual cold Storage lifecycle, bounded native boundary models (O0/O2)."""
import pathlib
import subprocess
import sys
import unittest
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from measure_cpp_baseline import suppress_windows_test_dialogs
from test_reist_probe_domain import function


class StorageRetirementTests(unittest.TestCase):
    def test_exact_interleaved_serial_records(self):
        from run_qemu_storage_retirement import ap_execution, exact_shuffle
        gui = "REIST_GUI DESKTOP_AUTOSTART_DISABLED explicit DESKTOP command required\n"
        ap = "REIST_STORAGE SERVICE_AP_EXEC cpu=3 generation=1\n"
        observed = "REIST_GUI DESKRTEIOSPT__ASUTOTROAGSET ASRETR_VDIICSEA_ABPL_EEXDE Ce xcppul=i3c igten eraDtEiSoKnT=O1P\n command required\n"
        self.assertTrue(exact_shuffle(observed, gui, ap))
        self.assertTrue(ap_execution(observed, "1"))
        for wrong in (observed[:-1], observed.replace("\n command", "\nx command"), "x" + observed,
                      observed.replace("cppul=i3", "cppul=i0"), observed[::-1]):
            self.assertFalse(ap_execution(wrong, "1"))
        self.assertFalse(ap_execution(observed, "2"))
        self.assertFalse(exact_shuffle(observed + "x", gui, ap))
        for cut in range(len(ap)+1):
            shuffled = ap[:cut] + gui + ap[cut:]
            self.assertTrue(exact_shuffle(shuffled, gui, ap))
        self.assertFalse(exact_shuffle("x"*500, "x"*250, "x"*250))

    def test_guest_retirement_order_and_generation(self):
        from run_qemu_storage_retirement import validate_retirement
        initial = "REIST_STORAGE SERVICE_IDENTITY pid=7 generation=3\n"
        retired = "REIST_STORAGE SERVICE_RETIRED pid=7 generation=3\n"
        replacement = "REIST_STORAGE SERVICE_IDENTITY pid=8 generation=4\n"
        self.assertIsNone(validate_retirement(initial + retired + replacement, 1))
        self.assertIsNone(validate_retirement(initial + "C:\\>" + retired + replacement, 1))
        for text in (initial + replacement + retired, initial + replacement,
                     initial + retired.replace("generation=3", "generation=4") + replacement,
                     initial + "noise " + retired + replacement,
                     initial + retired + initial):
            self.assertIsNotNone(validate_retirement(text, 1))
        self.assertIsNotNone(validate_retirement(initial + retired + replacement, 4))
        smp = "REIST_SMP SCHEDULER_READY cpus=4\n" + initial + "REIST_STORAGE SERVICE_AP_EXEC cpu=1 generation=3\n" + retired + replacement + "REIST_STORAGE SERVICE_AP_EXEC cpu=2 generation=4\n"
        self.assertIsNone(validate_retirement(smp, 4))
        self.assertIsNotNone(validate_retirement(smp.replace("cpu=2", "cpu=0"), 4))
        self.assertIsNone(validate_retirement(smp + "REIST_STORAGE SERVICE_RETIRED pid=8 generation=4\nREIST_STORAGE SERVICE_IDENTITY pid=9 generation=5\n", 4))

    def test_generation_exact_termination(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r339-storage-retirement" / ("identity-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source = (ROOT / "kernel/proc/process.c").read_text(encoding="utf-8")
        (evidence / "termination.inc").write_text("\n".join(function(source, signature) for signature in (
            "static int process_terminate_generation(", "int process_terminate(", "int process_terminate_identity(")), encoding="utf-8")
        for option in ("-O0", "-O2"):
            exe = evidence / (option + ".exe")
            for index, command in enumerate((
                ["gcc", "-std=c11", option, "-Wall", "-Wextra", "-Werror", "-DRETIREMENT_PROCESS_TEST", "-I", str(evidence),
                 str(ROOT / "test/storage_retirement_host.c"), "-o", str(exe)], [str(exe)])):
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (option + str(index) + ".log")).write_text(result.stdout+result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_actual_storage_lifecycle(self):
        suppress_windows_test_dialogs()
        evidence = ROOT / "build/codex-agent/r339-storage-retirement" / ("native-" + uuid.uuid4().hex)
        evidence.mkdir(parents=True)
        source = (ROOT / "kernel/init/storage_service.c").read_text(encoding="utf-8")
        pieces = [source[source.index("#define STORAGE_SERVICE_CONTROL_VERSION"):source.index("typedef struct __attribute__((packed))")]]
        for signature in (
            "static bool control_valid(", "static uint64_t deadline_after(",
            "static void lifecycle_fail(", "static bool lifecycle_enter(", "static void lifecycle_leave(",
            "static bool retirement_begin(", "static int retirement_step(",
            "static bool spawn_service(", "static bool storage_start_locked(uint64_t now_ms)",
            "bool storage_service_start(", "int storage_service_bind(",
            "bool storage_service_authorized(", "void storage_service_poll(",
            "bool storage_service_component_ready(", "static bool storage_down_locked(",
            "bool storage_service_component_down(", "bool storage_service_component_up(",
        ):
            if signature in source:
                pieces.append(function(source, signature))
        (evidence / "storage_lifecycle.inc").write_text("\n".join(pieces), encoding="utf-8")
        failures = []
        for option in ("-O0", "-O2"):
            exe = evidence / (option + ".exe")
            commands = [["gcc", "-std=c11", option, "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                         "-Wno-unused-variable", "-I", str(evidence), str(ROOT / "test/storage_retirement_host.c"), "-o", str(exe)], [str(exe)]]
            for index, command in enumerate(commands):
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                    timeout=90 if index == 0 else 30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                (evidence / (option + ("-compile.log" if index == 0 else "-run.log"))).write_text(result.stdout + result.stderr, encoding="utf-8")
                self.assertEqual(result.returncode if index == 0 else 0, 0, result.stdout + result.stderr)
                if index == 1 and result.returncode:
                    failures.append(option + ": " + result.stdout + result.stderr)
        self.assertFalse(failures, "\n".join(failures))


if __name__ == "__main__":
    unittest.main()
