"""Static contract checks for the compositor Surface broker."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "userspace/gui/compositor/desktop_surface_runtime.c").read_text()
HEADER = (ROOT / "userspace/gui/compositor/desktop_surface_runtime.h").read_text()


def test_runtime_is_bounded_and_nonblocking():
    assert "x86os_ipc_receive_timeout(client->endpoint, &ipc, 0U)" in SOURCE
    assert "x86os_ipc_send_timeout(endpoint, &ipc, 0U)" in SOURCE
    assert "x86os_malloc" not in SOURCE
    assert "x86os_display_" not in SOURCE
    assert "identity.pid != pid" in SOURCE
    assert "status == -32" in SOURCE
    assert "if (status == -32) return -32;" in SOURCE
    assert "ipc.version = X86OS_IPC_MESSAGE_VERSION" in SOURCE
    assert "ipc.struct_size = sizeof(ipc)" in SOURCE
    assert "if (status == -11) return 0;" in SOURCE
    assert "active != DESKTOP_SURFACE_RUNTIME_BOUND" in SOURCE


def test_runtime_has_explicit_lifecycle():
    assert "desktop_surface_runtime_initialize" in HEADER
    assert "desktop_surface_runtime_poll" in HEADER
    assert "desktop_surface_runtime_shutdown" in HEADER
    assert "x86os_ipc_close(client->endpoint)" in SOURCE
    assert "desktop_surface_runtime_reserve" in HEADER
    assert "desktop_surface_runtime_bind" in HEADER
    assert "desktop_surface_runtime_cancel" in HEADER
    assert "DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS 64U" in HEADER
    assert "request < X86OS_IPC_QUEUE_DEPTH" in SOURCE
    assert "x86os_yield()" in SOURCE
    assert "uint32_t input_sent[DESKTOP_SURFACE_RUNTIME_CAPACITY]" in SOURCE
    assert "input_status = send_pending_input(" in SOURCE
    assert "input_status == -11" in SOURCE
    assert "input_sent[i] = 1U" in SOURCE
    assert "active = DESKTOP_SURFACE_RUNTIME_RESERVED" in SOURCE
    assert "DESKTOP_SURFACE_RUNTIME_RETIRE_TIMEOUT_MS 1000U" in HEADER
    assert "DESKTOP_SURFACE_RUNTIME_RETIRING" in HEADER
    assert "static void disconnect_client" in SOURCE
    assert "desktop_surface_revoke_owner(manager, client->owner)" in SOURCE
    assert "if (input_status == -11) input_status = 0;" in SOURCE
    retire = SOURCE[SOURCE.index("static void retire_client"):
                    SOURCE.index("int desktop_surface_runtime_poll")]
    assert "x86os_process_identity_of" in retire
    assert retire.index("x86os_process_identity_of") < retire.index(
        "x86os_wait(client->owner.pid")
    assert "x86os_kill(client->owner.pid)" in retire


if __name__ == "__main__":
    test_runtime_is_bounded_and_nonblocking()
    test_runtime_has_explicit_lifecycle()
