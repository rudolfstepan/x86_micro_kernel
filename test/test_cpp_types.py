"""Behavior, lifetime and freestanding admission for the real C++ templates."""
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_user_program as builder
from measure_cpp_baseline import suppress_windows_test_dialogs


class CppTypesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.temporary = tempfile.TemporaryDirectory(prefix="reist-cpp-types-")
        cls.directory = Path(cls.temporary.name)
        cls.zig = builder.find_zig()
        cls.env = os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/r317/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"] = str(cls.directory / "cache")

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def compile(self, source, name="fixture", target=False, extra=()):
        path = source if isinstance(source, Path) else self.directory / (name + ".cpp")
        if not isinstance(source, Path):
            path.write_text(source, encoding="ascii")
        output = self.directory / (name + (".o" if target else ".exe"))
        if target:
            prefix = builder.freestanding_compile_prefix(self.zig)
        else:
            prefix = [str(self.zig), "cc", "-O2", "-UNDEBUG", "-fno-builtin"]
        command = [*prefix, *builder.cpp_compile_flags(), "-Wall", "-Wextra", "-Werror",
                   "-I" + str(ROOT / "userspace/cpp/include"),
                   "-I" + str(ROOT / "userspace/sdk/include"),
                   *extra, *(["-c"] if target else []), str(path), "-o", str(output)]
        result = subprocess.run(command, cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=90)
        return result, output

    def test_headers_compile_without_a_hosted_cpp_library(self):
        result, obj = self.compile(''.join('#include <reist/' + n + '.h>\n' for n in
            ("result", "optional", "span", "fixed_string", "fixed_vector", "unique_handle")), target=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        builder.validate_cpp_object(obj.read_bytes())

    def test_real_lifetime_bounds_and_ownership_behavior(self):
        for optimization in ("-O0", "-O2"):
            with self.subTest(optimization=optimization):
                result, exe = self.compile(ROOT / "test/test_cpp_types_host.cpp",
                                           "behavior" + optimization, extra=(optimization,))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                run = subprocess.run([str(exe)], cwd=ROOT, env=self.env,
                                     capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn("REIST_CPP_TYPES_HOST_OK", run.stdout)

    def test_i386_instantiations_link_without_allocator_or_cpp_runtime(self):
        result, obj = self.compile(ROOT / "test/test_cpp_types_host.cpp", "target_behavior",
                                   target=True, extra=("-DREIST_CPP_TYPES_FREESTANDING",))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = obj.read_bytes()
        builder.validate_cpp_object(data)
        # Inspect every ELF32 symbol table, not a text-pattern proxy for calls.
        offset = struct.unpack_from("<I", data, 32)[0]
        width, count = struct.unpack_from("<HH", data, 46)
        sections = [struct.unpack_from("<10I", data, offset + i * width) for i in range(count)]
        undefined = set()
        for section in sections:
            if section[1] != 2:
                continue
            strings_section = sections[section[6]]
            strings = data[strings_section[4]:strings_section[4] + strings_section[5]]
            for pos in range(section[4], section[4] + section[5], 16):
                name, _, _, info, _, index = struct.unpack_from("<IIIBBH", data, pos)
                if index == 0 and name and info >> 4:
                    undefined.add(strings[name:strings.index(0, name)].decode("ascii"))
        self.assertLessEqual(undefined, {"memcpy", "memset", "memmove"})
        primitives = '''extern "C" {
void *memset(void *p,int c,__SIZE_TYPE__ n) {
    volatile unsigned char *d=(volatile unsigned char*)p;
    for(__SIZE_TYPE__ i=0;i<n;++i)d[i]=(unsigned char)c; return p;
}
void *memcpy(void *p,const void *s,__SIZE_TYPE__ n) {
    volatile unsigned char *d=(volatile unsigned char*)p;
    const volatile unsigned char *a=(const volatile unsigned char*)s;
    for(__SIZE_TYPE__ i=0;i<n;++i)d[i]=a[i]; return p;
}
void *memmove(void *p,const void *s,__SIZE_TYPE__ n) {
    volatile unsigned char *d=(volatile unsigned char*)p;
    const volatile unsigned char *a=(const volatile unsigned char*)s;
    if((__UINTPTR_TYPE__)p>(__UINTPTR_TYPE__)s) { for(__SIZE_TYPE__ i=n;i;--i)d[i-1]=a[i-1]; }
    else { for(__SIZE_TYPE__ i=0;i<n;++i)d[i]=a[i]; } return p;
}}
'''
        result, memory = self.compile(primitives, "memory", target=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        elf = self.directory / "types.elf"
        link = subprocess.run([str(self.zig), "cc", "-target", "x86-freestanding", "-nostdlib",
                               "-Wl,-e,main", str(obj), str(memory), "-o", str(elf)],
                              cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(link.returncode, 0, link.stdout + link.stderr)
        builder.validate_cpp_object(elf.read_bytes())

    def test_real_guest_probe_obeys_ipc_sender_exclusion(self):
        # Execute the actual new probe, not a rewritten approximation. The model
        # reflects ipc.c receivable_offset: messages from this exact process
        # generation are not receivable by itself. Other fault/reap paths are
        # exercised by the required full guest gate, not mocked here.
        probe = (ROOT / "userspace/programs/cpptest.cpp").read_text()
        includes = probe.split("namespace {", 1)[0]
        objects = probe.split("unsigned constructed, destroyed;", 1)[1].split("static_assert", 1)[0]
        functions = probe.split("int send(x86os_ipc_handle_t endpoint)", 1)[1].split("int wait_child(", 1)[0]
        model = r'''
unsigned active_handle, handle_generation, pending;
extern "C" int x86os_ipc_create(x86os_ipc_handle_t* h) {
    if(active_handle) return -28;
    *h=active_handle=(++handle_generation<<8)|1; return 0;
}
extern "C" int x86os_ipc_close(x86os_ipc_handle_t h) {
    if(h!=active_handle || !h) return -13;
    active_handle=0; pending=0; return 0;
}
extern "C" int x86os_ipc_send_timeout(x86os_ipc_handle_t h,const x86os_ipc_message_t* m,uint32_t) {
    if(h!=active_handle || !h) return -13;
    if(m->length!=1 || m->payload[0]!=0x43 || pending) return -22;
    pending=1; return 0;
}
extern "C" int x86os_ipc_receive_timeout(x86os_ipc_handle_t h,x86os_ipc_message_t*,uint32_t) {
    return h && h==active_handle ? -11 : -13;
}
extern "C" int x86os_memory_stats(x86os_memory_stats_t* stats) {
    *stats={}; stats->allocated_frame_bytes=4096; return 0;
}
extern "C" int reist_libc_stats(reist_libc_stats_t* stats) {
    stats->capacity=stats->live_objects=stats->live_bytes=0; return 0;
}
extern "C" void x86os_puts(const char*) {}
extern "C" void x86os_exit(int code) { _Exit(code); }
int main() { return bounded_types() && !active_handle && !pending && handle_generation==2 ? 0 : 1; }
'''
        code = includes + 'extern "C" void _Exit(int) __attribute__((noreturn));\nunsigned constructed, destroyed;' + objects
        code += "int send(x86os_ipc_handle_t endpoint)" + functions + model
        result, exe = self.compile(code, "guest_probe", extra=("-I" + str(ROOT / "userspace/libc/include"),))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = subprocess.run([str(exe)], env=self.env, cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_unsafe_operations_are_compile_time_rejected(self):
        headers = ''.join('#include <reist/' + n + '.h>\n' for n in
                          ("optional", "result", "span", "fixed_string", "fixed_vector", "unique_handle"))
        cases = {
            "throwing_ctor": 'struct X { X() noexcept(false); }; void f(){ reist::Optional<X> x; (void)x.try_emplace(); }',
            "throwing_dtor": 'struct X { ~X() noexcept(false); }; void f(){ reist::Optional<X> x; }',
            "const_value": 'void f(){ reist::Optional<const int> x; }',
            "array_value": 'void f(){ reist::FixedVector<int[2],2> x; }',
            "optional_borrow": 'void f(){ auto p = reist::Optional<int>().get(); (void)p; }',
            "result_borrow": 'void f(){ auto p = reist::Result<int,int>::success(1).value_if(); (void)p; }',
            "vector_borrow": 'void f(){ auto p = reist::FixedVector<int,2>().at(0); (void)p; }',
            "string_borrow": 'void f(){ auto p = reist::FixedString<2>().c_str(); (void)p; }',
            "span_const": 'void f(){ const int a[2]{}; reist::Span<int> s(a); }',
            "vector_overflow": 'void f(){ reist::FixedVector<int,SIZE_MAX> v; }',
            "string_overflow": 'void f(){ reist::FixedString<SIZE_MAX> v; }',
            "no_handle_policy": 'void f(){ reist::UniqueHandle<int> owner; }',
            "throwing_release": 'struct P { static int invalid() noexcept; static bool is_valid(int) noexcept; '
                                'static bool equal(int,int) noexcept; static void close(int); }; '
                                'void f(){ reist::UniqueHandle<int,P> owner; }',
            "copy_owner": 'struct P { static int invalid() noexcept; static bool is_valid(int) noexcept; '
                          'static bool equal(int,int) noexcept; static void close(int) noexcept; }; '
                          'void f(){ reist::UniqueHandle<int,P> a; auto b=a; }',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                result, _ = self.compile(headers + source, name, target=True)
                self.assertNotEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
