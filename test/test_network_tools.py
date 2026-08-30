import unittest
import hashlib
import os
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class NetworkToolsSourceTests(unittest.TestCase):
    def test_kernel_socket_tables_use_smp_locks_and_atomic_wait_transfer(self):
        udp = (ROOT / "drivers/net/net_socket.c").read_text()
        tcp = (ROOT / "drivers/net/tcp_socket.c").read_text()
        self.assertIn("socket_state_lock = SPINLOCK_INIT", udp)
        self.assertIn("spinlock_acquire_irq(&socket_state_lock)", udp)
        self.assertIn("wait_queue_block_until_spinlocked(", udp)
        self.assertIn("tcp_state_lock = SPINLOCK_INIT", tcp)
        self.assertIn("spinlock_acquire_irq(&tcp_state_lock)", tcp)
        self.assertIn("wait_queue_block_until_spinlocked(", tcp)

    def test_standalone_network_tools_are_built_and_installed(self):
        programs = (ROOT / "scripts/build_system_programs.py").read_text()
        makefile = (ROOT / "Makefile").read_text()
        windows = (ROOT / "scripts/build-windows.ps1").read_text()
        for name in ("ifconfig", "ping", "netstat", "udp", "nslookup", "nc",
                     "httpd", "curl"):
            upper = name.upper() + ".PRG"
            source = f'userspace/programs/{name}.c'
            self.assertIn(upper, programs)
            self.assertIn(source, programs)
            install = f'usr/bin/{name}.prg' if name == "curl" else f'sbin/{name}.prg'
            self.assertIn(install, makefile)
            self.assertIn(f"'{install}'", windows)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_curl_http_parser_host_behavior(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "curl_http_test"
            if os.name == "nt": executable = executable.with_suffix(".exe")
            subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT),
                str(ROOT / "userspace/programs/curl_http.c"),
                str(ROOT / "test/test_curl_http_host.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_curl_is_bounded_and_uses_public_abis(self):
        source = (ROOT / "userspace/programs/curl.c").read_text()
        self.assertIn("CURL_HARD_MAX_BYTES", source)
        self.assertIn("CURL_TRANSFER_DEADLINE_MS", source)
        self.assertIn("x86os_dns_resolve", source)
        self.assertIn("x86os_tcp_connect", source)
        self.assertIn("x86os_tcp_receive", source)
        self.assertIn("x86os_rename(temporary, options.output)", source)
        self.assertIn("reist_tls_client_open", source)
        self.assertIn("REIST_CURL_SCHEME_HTTPS", source)
        self.assertIn("reist_tls_client_write", source)
        self.assertNotIn("x86os_syscall(", source)
        runner = (ROOT / "scripts/run_qemu_smoke.py").read_text()
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text()
        self.assertIn("CURL_TEST_COMMAND", runner)
        self.assertIn("serve_curl_test_client", runner)
        self.assertIn("--expect-curl-client", runner)
        self.assertIn("'curl-https-client'", runtime)
        self.assertIn('"qemu32,+rdrand"', runner)

    def test_reist_tls_authenticated_host_behavior(self):
        zig = Path(r"C:\tools\zig-x86_64-windows-0.16.0\zig.exe")
        if not zig.is_file():
            located = shutil.which("zig")
            if located is None:
                self.skipTest("Zig is required for the Mbed TLS host proof")
            zig = Path(located)
        scripts = ROOT / "scripts"
        sys.path.insert(0, str(scripts))
        try:
            import build_user_sdk as sdk
        finally:
            sys.path.pop(0)
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            vendor = sdk.extract_mbedtls(temporary / "mbedtls")
            executable = temporary / "reist_tls_host"
            if os.name == "nt":
                executable = executable.with_suffix(".exe")
            includes = (
                ROOT / "userspace/sdk/include",
                ROOT / "userspace/tls/include",
                ROOT / "userspace/tls/lib",
                vendor,
                vendor / "include",
                vendor / "library",
                vendor / "tf-psa-crypto/include",
                vendor / "tf-psa-crypto/core",
                vendor / "tf-psa-crypto/drivers/builtin/include",
                vendor / "tf-psa-crypto/drivers/builtin/src",
                vendor / "tf-psa-crypto/extras",
                vendor / "tf-psa-crypto/utilities",
                vendor / "tf-psa-crypto/dispatch",
                vendor / "tf-psa-crypto/platform",
            )
            command = [
                str(zig), "cc", "-O1", "-std=c11", "-ffunction-sections",
                "-fdata-sections", '-DMBEDTLS_CONFIG_FILE="reist_tls_config.h"',
                '-DTF_PSA_CRYPTO_CONFIG_FILE="reist_tls_config.h"',
            ]
            for include in includes:
                command.extend(("-I", str(include)))
            command.extend(str(source) for source in (
                ROOT / "userspace/tls/lib/reist_tls.c",
                ROOT / "userspace/tls/lib/reist_tls_platform.c",
                ROOT / "userspace/tls/lib/reist_tls_trust_anchors.c",
                ROOT / "test/test_reist_tls_host.c",
                *sdk.mbedtls_sources(vendor),
            ))
            command.extend(("-Wl,--gc-sections", "-o", str(executable)))
            if os.name == "nt":
                command.append("-lws2_32")
            environment = os.environ.copy()
            environment["ZIG_GLOBAL_CACHE_DIR"] = str(temporary / "zig-global")
            environment["ZIG_LOCAL_CACHE_DIR"] = str(temporary / "zig-local")
            subprocess.run(
                command, check=True, cwd=ROOT, env=environment)

            fixtures = ROOT / "test/fixtures"
            cases = (
                ("curl_tls_server.pem", "success", "local-ca",
                 ssl.TLSVersion.TLSv1_2, None),
                ("curl_tls_server.pem", "success", "local-ca",
                 ssl.TLSVersion.TLSv1_3, None),
                ("curl_tls_wrong_host.pem", "failure", "local-ca", None, None),
                ("curl_tls_expired.pem", "failure", "local-ca", None, None),
                ("curl_tls_server.pem", "failure", "system-ca", None, None),
                ("curl_tls_server.pem", "record-failure", "local-ca",
                 None, None),
                (None, "failure", "local-ca", None, b"not-a-tls-record"),
                (None, "failure", "local-ca", None, b"\x16\x03\x03\x00\x20bad"),
                (None, "failure", "local-ca", None, b"\x17\x03\x03\x00\x04evil"),
            )
            for certificate, expected, trust, version, malformed in cases:
                listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                listener.bind(("127.0.0.1", 0))
                listener.listen(1)
                port = listener.getsockname()[1]
                server_errors = []

                def serve() -> None:
                    try:
                        peer, _ = listener.accept()
                        with peer:
                            if malformed is not None:
                                peer.recv(4096)
                                peer.sendall(malformed)
                                return
                            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                            context.load_cert_chain(
                                fixtures / certificate,
                                fixtures / "curl_tls_server.key")
                            if version is not None:
                                context.minimum_version = version
                                context.maximum_version = version
                            with context.wrap_socket(peer, server_side=True) as tls:
                                if expected in ("success", "record-failure"):
                                    if tls.recv(4) != b"ping":
                                        raise RuntimeError("unexpected TLS payload")
                                    tls.sendall(b"pong")
                    except (OSError, ssl.SSLError, RuntimeError) as error:
                        if expected == "success":
                            server_errors.append(error)
                    finally:
                        listener.close()

                thread = threading.Thread(target=serve)
                thread.start()
                proof = subprocess.run([
                    str(executable), str(port), expected, trust,
                    str(fixtures / "curl_tls_ca.pem"),
                ], check=False, cwd=ROOT, timeout=15, text=True,
                   capture_output=True)
                thread.join(timeout=5)
                self.assertFalse(thread.is_alive())
                self.assertFalse(server_errors, repr(server_errors))
                self.assertEqual(
                    proof.returncode, 0,
                    f"{certificate or 'malformed'}: {proof.stderr.strip()}")

    def test_reist_tls_contract_is_bounded_and_pinned(self):
        public = (ROOT / "userspace/tls/include/reist/tls.h").read_text()
        config = (ROOT / "userspace/tls/lib/reist_tls_config.h").read_text()
        implementation = (ROOT / "userspace/tls/lib/reist_tls.c").read_text()
        platform = (ROOT / "userspace/tls/lib/reist_tls_platform.c").read_text()
        sdk = (ROOT / "scripts/build_user_sdk.py").read_text()
        programs = (ROOT / "scripts/build_system_programs.py").read_text()
        for marker in (
            "REIST_TLS_CONTEXT_BYTES (512U * 1024U)",
            "REIST_TLS_HEAP_BUDGET_BYTES (4U * 1024U * 1024U)",
            "REIST_TLS_MAX_ALLOCATION_BYTES (512U * 1024U)",
            "reist_tls_monotonic_fn", "reist_tls_entropy_fn",
            "reist_tls_allocate_fn", "reist_tls_free_fn",
        ):
            self.assertIn(marker, public)
        for marker in (
            "MBEDTLS_SSL_PROTO_TLS1_2", "MBEDTLS_SSL_PROTO_TLS1_3",
            "MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED",
            "MBEDTLS_X509_RSASSA_PSS_SUPPORT",
            "MBEDTLS_PSA_DRIVER_GET_ENTROPY", "MBEDTLS_CTR_DRBG_C",
            "PSA_WANT_ALG_GCM",
        ):
            self.assertIn(marker, config)
        for forbidden in ("MBEDTLS_SSL_RENEGOTIATION", "MBEDTLS_ARC4_C",
                          "MBEDTLS_DES_C", "MBEDTLS_CIPHER_MODE_CBC"):
            self.assertNotIn(forbidden, config)
        self.assertIn("MBEDTLS_SSL_VERIFY_REQUIRED", implementation)
        self.assertIn("mbedtls_ssl_set_hostname", implementation)
        self.assertIn("mbedtls_ssl_get_verify_result", implementation)
        self.assertIn("reist_tls_platform_bind", implementation)
        self.assertIn("allocation_arena", platform)
        self.assertIn("rdrand_supported", platform)
        self.assertIn("attempt < 16U", platform)
        self.assertIn("sample == previous", platform)
        self.assertIn("libreisttls.a", sdk)
        self.assertIn("sdk.tls_library", programs)
        archive = ROOT / "third_party/mbedtls-4.1.1.tar.bz2"
        self.assertEqual(
            hashlib.sha256(archive.read_bytes()).hexdigest(),
            "3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c")

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
        self.assertIn("reist_vfs_readdir_at", source)
        self.assertIn("reist_vfs_stat", source)
        self.assertIn("reist_vfs_file_open", source)
        self.assertIn("reist_vfs_file_read", source)
        self.assertIn("reist_vfs_file_close", source)
        self.assertNotIn("x86os_stat(", source)
        self.assertNotIn("x86os_open(", source)
        self.assertNotIn("x86os_read(", source)
        self.assertNotIn("x86os_close(", source)
        self.assertNotIn("x86os_readdir", source)
        self.assertIn("HTTPD_VFS_STAT_CLIENT_OK", source)
        self.assertIn("HTTPD_VFS_READ_SESSION_OK", source)
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
        runtime = (ROOT / "scripts/test-reist-runtime.ps1").read_text()
        programs = (ROOT / "scripts/build_system_programs.py").read_text()
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
        self.assertIn("HTTP_VFS_STAT_MARKER", runner)
        self.assertIn("HTTP_VFS_READ_SESSION_MARKER", runner)
        self.assertIn('b"/about.txt"', runner)
        self.assertIn('b"REIST OS demo HTTP server\\n"', runner)
        self.assertIn("'http-server'", runtime)
        self.assertIn("'--expect-http-server'", runtime)
        mapping = programs[programs.index('"HTTPD.PRG"'):
                           programs.index('"EDIT.PRG"')]
        self.assertIn("vfs_stat_client.c", mapping)
        self.assertIn("vfs_read_client.c", mapping)
        self.assertIn("vfs_file_client.c", mapping)
        self.assertIn("vfs_path.c", mapping)

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
