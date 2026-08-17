import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class BootServiceReadyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.kernel = read("kernel/init/kernel.c")
        cls.supervisor = read("kernel/init/supervisor.c")
        cls.supervisor_h = read("include/kernel/supervisor.h")
        cls.sdk_h = read("userspace/sdk/include/x86os.h")
        cls.probe = read("userspace/programs/reist_probe.c")

    def test_report_abi_is_append_only_and_published_after_udp_self_test(self):
        self.assertIn("REIST_REPORT_SERVICE_READY 12U", self.supervisor_h)
        self.assertIn("X86OS_REIST_REPORT_SERVICE_READY 12U", self.sdk_h)
        stale_unbind = self.probe.index("x86os_reist_udp_unbind(stale_binding)")
        ready_report = self.probe.index("X86OS_REIST_REPORT_SERVICE_READY")
        service_loop = self.probe.index("for (;;)", ready_report)
        self.assertLess(stale_unbind, ready_report)
        self.assertLess(ready_report, service_loop)

    def test_readiness_is_protected_and_generation_reset(self):
        self.assertIn("uint32_t service_ready;", self.supervisor_h)
        self.assertIn("SUPERVISOR_PROBE_CONTROL_VERSION 2U", self.supervisor_h)
        ready = function(self.supervisor, "bool supervisor_probe_ready(")
        self.assertIn("control.service_ready != 0U", ready)
        report = function(self.supervisor, "int supervisor_probe_report(")
        self.assertIn("REIST_REPORT_SERVICE_READY", report)
        self.assertIn("control.service_ready = 1U", report)
        fence = function(self.supervisor, "static bool probe_fence_apply(")
        spawn = function(self.supervisor, "static bool probe_spawn_next(")
        self.assertIn("control.service_ready = 0U", fence)
        self.assertIn("control.service_ready = 0U", spawn)

    def test_no_nic_still_waits_bounded_before_boot_and_shell(self):
        configure = function(
            self.kernel, "static void configure_network_after_service(")
        wait = configure.index("while (!supervisor_probe_ready()")
        no_nic = configure.index("if (!netdev_available())")
        self.assertLess(wait, no_nic)
        self.assertIn("10000U", configure)
        self.assertIn("local-only", configure)
        self.assertNotIn("settle_deadline", configure)
        call = self.kernel.index("configure_network_after_service();")
        boot_ok = self.kernel.index('printf("BOOT_OK\\n")', call)
        shell = self.kernel.index(
            'start_userspace_program(multiboot_info, "bin/shell.prg"', boot_ok)
        self.assertLess(call, boot_ok)
        self.assertLess(boot_ok, shell)


if __name__ == "__main__":
    unittest.main()
