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
    assert "active != 1U" in SOURCE


def test_runtime_has_explicit_lifecycle():
    assert "desktop_surface_runtime_initialize" in HEADER
    assert "desktop_surface_runtime_poll" in HEADER
    assert "desktop_surface_runtime_shutdown" in HEADER
    assert "x86os_ipc_close(runtime->clients[i].endpoint)" in SOURCE
    assert "desktop_surface_runtime_reserve" in HEADER
    assert "desktop_surface_runtime_bind" in HEADER
    assert "desktop_surface_runtime_cancel" in HEADER
    assert "DESKTOP_SURFACE_RUNTIME_DRAIN_ROUNDS 64U" in HEADER
    assert "request < X86OS_IPC_QUEUE_DEPTH" in SOURCE
    assert "x86os_yield()" in SOURCE
    assert "active = 2U" in SOURCE


if __name__ == "__main__":
    test_runtime_is_bounded_and_nonblocking()
    test_runtime_has_explicit_lifecycle()
