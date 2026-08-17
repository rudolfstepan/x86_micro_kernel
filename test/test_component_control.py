import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ComponentControlContracts(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_registry_is_static_bounded_and_protects_core(self):
        header = self.read("include/kernel/component_control.h")
        source = self.read("kernel/init/component_control.c")
        self.assertIn("COMPONENT_CONTROL_MAX_COMPONENTS 7U", header)
        self.assertIn("static const component_descriptor_t components", source)
        for name in ("scheduler", "monotonic-clock", "interrupt-core",
                     "root-storage"):
            self.assertIn(f'"{name}"', source)
        self.assertIn("COMPONENT_FLAG_PROTECTED", source)
        self.assertIn('"network-service"', source)
        self.assertIn("return COMPONENT_EPROTECTED;", source)
        self.assertNotIn("k_malloc", source)

    def test_lifecycle_is_generation_and_dependency_safe(self):
        source = self.read("kernel/init/component_control.c")
        self.assertIn("request->expected_generation", source)
        self.assertIn("state->generation == UINT32_MAX", source)
        self.assertNotIn("generation == UINT32_MAX ? 1U", source)
        self.assertIn("dependencies_ready", source)
        self.assertIn("ready_dependent_exists", source)
        self.assertIn("COMPONENT_STATE_QUIESCING", source)
        self.assertIn("COMPONENT_STATE_STARTING", source)
        self.assertIn("__sync_lock_test_and_set", source)
        self.assertIn("pit_monotonic_ms", source)
        self.assertIn("COMPONENT_CONTROL_TIMEOUT_MAX_MS", source)

    def test_supported_backends_close_producers_before_cleanup(self):
        network = self.read("drivers/net/netdev.c")
        storage = self.read("kernel/init/storage_service.c")
        self.assertIn("netdev_administratively_enabled = false;", network)
        self.assertIn("netdev_component_down", network)
        self.assertIn("netdev_component_up", network)
        self.assertIn("service_administratively_enabled = false;", storage)
        self.assertIn("storage_request_unbind_service", storage)
        self.assertIn("storage_service_component_down", storage)
        self.assertIn("storage_service_component_up", storage)

    def test_component_syscall_is_append_only_and_default_deny(self):
        header = self.read("include/kernel/component_control.h")
        sdk = self.read("userspace/sdk/include/x86os.h")
        process = self.read("kernel/proc/process.c")
        syscall = self.read("kernel/syscall/syscall_table.c")
        self.assertIn("COMPONENT_CONTROL_SYSCALL 91U", header)
        self.assertIn("X86OS_SYS_COMPONENT_CONTROL = 91", sdk)
        self.assertIn("PROCESS_DOMAIN_COMPONENT_ADMIN", process)
        self.assertIn('strcmp(resolved, "/SVCCTL.PRG") == 0', process)
        self.assertIn("SYS_COMPONENT_CONTROL", syscall)
        self.assertIn("syscall_component_control", syscall)

    def test_storage_manual_start_has_a_visible_diagnostic(self):
        service = self.read("examples/userspace/storage_service.c")
        self.assertIn("STORAGE SERVICE_BIND_FAILED code=", service)
        self.assertIn("Use svcctl", service)


if __name__ == "__main__":
    unittest.main()
