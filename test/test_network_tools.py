import unittest
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class NetworkToolsSourceTests(unittest.TestCase):
    def test_standalone_network_tools_are_built_and_installed(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text()
        makefile = (ROOT / "Makefile").read_text()
        windows = (ROOT / "scripts/build-windows.ps1").read_text()
        for name in ("ifconfig", "ping", "netstat", "udp", "nslookup", "nc",
                     "httpd"):
            upper = name.upper() + ".PRG"
            source = f'userspace/programs/{name}.c'
            self.assertIn(upper, programs)
            self.assertIn(source, programs)
            self.assertIn(f'sbin/{name}.prg', makefile)
            self.assertIn(f"'sbin/{name}.prg'", windows)

    def test_tools_use_public_network_control_abi(self):
        for name in ("ifconfig", "ping", "netstat"):
            source = (ROOT / f"userspace/programs/{name}.c").read_text()
            self.assertIn("x86os_network_control", source)
            self.assertNotIn("x86os_syscall(", source)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_bounded_udp_socket_host_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "net_socket_test"
            if os.name == "nt":
                executable = executable.with_suffix(".exe")
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "drivers/net/net_socket.c"),
                str(ROOT / "test/test_net_socket_host.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_udp_socket_abi_is_append_only_and_service_mediated(self):
        sdk = (ROOT / "userspace/sdk/include/x86os.h").read_text()
        libc = (ROOT / "lib/libc/stdlib.h").read_text()
        process = (ROOT / "kernel/proc/process.c").read_text()
        probe = (ROOT / "userspace/programs/reist_probe.c").read_text()
        self.assertIn("X86OS_SYS_NETWORK_CONTROL = 96,", sdk)
        for number, name in enumerate((
            "UDP_SOCKET_CONTROL", "UDP_SOCKET_SENDTO",
            "UDP_SOCKET_RECVFROM", "UDP_SOCKET_INGRESS"), start=97):
            self.assertIn(f"X86OS_SYS_{name} = {number}", sdk)
            self.assertIn(f"#define SYS_{name} {number}", libc)
        self.assertIn("SYS_UDP_SOCKET_INGRESS", process)
        self.assertIn("x86os_udp_socket_ingress", probe)
        self.assertIn("socket_result != -105", probe)
        for number, name in enumerate((
            "TCP_SOCKET_CONTROL", "TCP_SOCKET_CONNECT", "TCP_SOCKET_SEND",
            "TCP_SOCKET_RECEIVE", "TCP_SOCKET_INGRESS", "TCP_SOCKET_LISTEN",
            "TCP_SOCKET_ACCEPT"), start=101):
            self.assertIn(f"X86OS_SYS_{name} = {number}", sdk)
            self.assertIn(f"#define SYS_{name} {number}", libc)
        self.assertIn("SYS_TCP_SOCKET_INGRESS", process)
        self.assertIn("x86os_tcp_socket_ingress", probe)
        process_h = (ROOT / "kernel/proc/process.h").read_text()
        syscalls = (ROOT / "kernel/syscall/syscall_table.c").read_text()
        self.assertIn("PROCESS_DESCRIPTOR_UDP_SOCKET", process_h)
        self.assertIn("PROCESS_DESCRIPTOR_TCP_SOCKET", process_h)
        self.assertIn("process_descriptor_install", syscalls)
        self.assertIn("process_descriptor_resolve", syscalls)
        self.assertIn("tcp_socket_listen", syscalls)
        self.assertIn("tcp_socket_accept", syscalls)

    def test_http_server_is_bounded_and_uses_public_tcp_abi(self):
        source = (ROOT / "userspace/programs/httpd.c").read_text()
        self.assertIn("HTTP_MAX_REQUESTS 32U", source)
        self.assertIn("HTTP_REQUEST_CAPACITY 1024U", source)
        self.assertIn("HTTP_DIRECTORY_MAX_ENTRIES 32U", source)
        self.assertIn("HTTP_FILE_MAX_BYTES 4096U", source)
        self.assertIn("x86os_tcp_listen", source)
        self.assertIn("x86os_tcp_accept", source)
        self.assertIn("x86os_readdir_batch", source)
        self.assertIn('static const char root[] = "/htdocs"', source)
        self.assertIn("HTTP/1.0 200 OK", source)
        self.assertIn("port_value = 8080U, requests = 0U", source)
        self.assertIn("HTTP_ACCEPT_TIMEOUT_MS 250U", source)
        self.assertIn("x86os_getchar_nonblocking() == 0x03", source)
        self.assertIn("if (client_result == 0) ++served", source)
        self.assertNotIn("x86os_syscall(", source)
        makefile = (ROOT / "Makefile").read_text()
        windows = (ROOT / "scripts/build-windows.ps1").read_text()
        for name in ("about.txt", "readme.txt", "status.jsn"):
            self.assertTrue((ROOT / "htdocs" / name).is_file())
            self.assertIn(f"htdocs/{name}=htdocs/{name}", makefile)
            self.assertIn(name, windows)
        runner = (ROOT / "scripts/run_qemu_smoke.py").read_text()
        detach = runner.index(
            'qemu_monitor_command(process, "netdev_del reistuserport")')
        launch = runner.index("inject_ps2_command(process, HTTP_TEST_COMMAND)")
        self.assertLess(detach, launch)
        self.assertIn("def serve_http_test_client(", runner)
        self.assertIn("HTTP_TEST_REQUESTS = 12", runner)
        self.assertIn('HTTP_TEST_COMMAND = "httpd 8080"', runner)
        self.assertIn("for request_index in range(HTTP_TEST_REQUESTS)", runner)
        self.assertIn('inject_ps2_key(process, "ctrl-c")', runner)
        self.assertIn('error = "httpd exited before Ctrl+C"', runner)
        self.assertIn('b"about.txt\\n"', runner)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_dns_cname_and_compression_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "dns_test"
            if os.name == "nt":
                executable = executable.with_suffix(".exe")
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-I", str(ROOT),
                "-I", str(ROOT / "userspace/sdk/include"),
                str(ROOT / "userspace/sdk/reist_dns.c"),
                str(ROOT / "test/test_dns_host.c"),
                "-Wl,--gc-sections", "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_tcp_connect_stream_and_close_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "tcp_test"
            if os.name == "nt": executable = executable.with_suffix(".exe")
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DREIST_HOST_TEST", "-I", str(ROOT),
                str(ROOT / "drivers/net/tcp_socket.c"),
                str(ROOT / "test/test_tcp_socket_host.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_tcp_ring3_parser_checks_checksum_and_lengths(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "tcp_parser_test"
            if os.name == "nt": executable = executable.with_suffix(".exe")
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "userspace/sdk/include"),
                str(ROOT / "userspace/sdk/reist_ipv4_parser.c"),
                str(ROOT / "userspace/sdk/reist_tcp_parser.c"),
                str(ROOT / "test/test_reist_tcp_parser_host.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
