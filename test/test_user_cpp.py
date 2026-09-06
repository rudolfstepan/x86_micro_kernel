"""Opt-in C++20 profile, ABI admission and actual runtime regressions."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_user_program as builder


class CppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.zig = builder.find_zig()
        cls.workspace = tempfile.TemporaryDirectory(prefix="reist-cpp-test-")
        cls.directory = Path(cls.workspace.name)
        cls.env = os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/r316/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"] = str(cls.directory / "cache")
        cls.includes = [ROOT / "userspace/cpp/include", ROOT / "userspace/libc/include"]

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def command(self, args, success=True):
        result = subprocess.run([str(a) for a in args], env=self.env, cwd=ROOT,
                                capture_output=True, text=True, timeout=90)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def compile(self, code, name="fixture.cpp", extra=(), profile=True):
        source = self.directory / name
        source.write_text(code, encoding="ascii")
        obj = source.with_suffix(".o")
        flags = builder.cpp_compile_flags() if profile else ["-std=c++20", "-nostdinc++"]
        result = subprocess.run([*builder.freestanding_compile_prefix(self.zig, self.includes),
                                 *flags, *extra, "-c", str(source), "-o", str(obj)],
                                env=self.env, cwd=ROOT, capture_output=True, text=True, timeout=60)
        return result, obj

    def test_profile_is_explicit_and_has_no_hosted_runtime(self):
        flags = builder.cpp_compile_flags()
        for flag in ("-std=c++20", "-fno-exceptions", "-fno-rtti",
                     "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
                     "-nostdinc++", "-Werror=global-constructors",
                     "-Werror=exit-time-destructors"):
            self.assertIn(flag, flags)

    def test_translation_rejects_unsupported_lifetimes_and_language_features(self):
        cases = {
            "global": "extern int f(); int value=f();",
            "destructor": "struct A { ~A(); }; A a;",
            "exceptions": "void f() { throw 1; }",
            "rtti": "struct A { virtual ~A(); }; struct B:A {}; B* f(A*p) { return dynamic_cast<B*>(p); }",
            "stl": "#include <vector>\nstd::vector<int> a;",
        }
        for name, code in cases.items():
            with self.subTest(name=name):
                result, _ = self.compile(code, name + ".cpp")
                self.assertNotEqual(result.returncode, 0, result.stderr)

    def test_object_admission_rejects_bypassed_diagnostics_and_dead_code(self):
        cases = {
            "global": "extern int f(); int value=f();",
            "local": "extern int f(); int g() { static int v=f(); return v; }",
            "destructor": "struct A { ~A(); }; A a;",
            "tls": "thread_local int v; int f() { return ++v; }",
            "atexit": 'extern "C" int atexit(void(*)()); void g(); int f(){ return atexit(g); }',
            "rtti": "struct A { virtual ~A(); }; struct B:A {}; B* f(A*p) { return dynamic_cast<B*>(p); }",
            "exceptions": "void f() { throw 1; }",
        }
        for name, code in cases.items():
            with self.subTest(name=name):
                result, obj = self.compile(code, "unguarded_" + name + ".cpp", profile=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                with self.assertRaises(ValueError):
                    builder.validate_cpp_object(obj.read_bytes())
                archive = obj.with_suffix(".a")
                self.command([self.zig, "ar", "rcs", archive, obj])
                with self.assertRaises(ValueError):
                    builder.validate_cpp_object(archive.read_bytes())
        result, obj = self.compile(cases["local"], "unguarded_local_no_guards.cpp")
        self.assertEqual(result.returncode, 0, result.stderr)
        with self.assertRaisesRegex(ValueError, "_ZGV"):
            builder.validate_cpp_object(obj.read_bytes())

    def test_constant_initialization_and_explicit_object_lifetimes_are_admitted(self):
        result, obj = self.compile('#include <new>\n#include <x86os.h>\n'
            'constinit int v=42; struct A { int x; constexpr A():x(7){} }; constinit A a; '
            'int f(){ static constinit int v=5; return v+a.x; } '
            'struct B { virtual int f() noexcept {return 1;} virtual ~B() noexcept {} }; '
            'int g(){ B b; return b.f(); }')
        self.assertEqual(result.returncode, 0, result.stderr)
        builder.validate_cpp_object(obj.read_bytes())
        for data in (b"", b"!<thin>\n", obj.read_bytes()[:51], b"!<arch>\n" + b"x" * 60):
            with self.assertRaises(ValueError):
                builder.validate_cpp_object(data)
        malformed = bytearray(obj.read_bytes())
        struct.pack_into("<I", malformed, 32, len(malformed) + 1)
        with self.assertRaises(ValueError):
            builder.validate_cpp_object(malformed)

    def test_real_runtime_with_private_backing_and_process_local_failures(self):
        prefix = [str(self.zig), "cc", "-O1", "-UNDEBUG", "-fno-builtin",
                  "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "userspace/sdk/include"),
                  *["-I" + str(p) for p in self.includes]]
        rename = ["-D" + n + "=reist_cpp_test_" + n for n in ("malloc", "calloc", "realloc", "free")]
        heap, runtime, test = [self.directory / n for n in ("heap.o", "runtime.o", "test.o")]
        self.command([*prefix, *rename, "-std=c11", "-c", ROOT / "userspace/libc/lib/heap.c", "-o", heap])
        self.command([*prefix, *rename, *builder.cpp_compile_flags(), "-c", ROOT / "userspace/cpp/runtime.cpp", "-o", runtime])
        self.command([*prefix, *builder.cpp_compile_flags(), "-c", ROOT / "test/test_user_cpp_host.cpp", "-o", test])
        exe = self.directory / "cpp-host.exe"
        self.command([self.zig, "cc", heap, runtime, test, "-o", exe])
        self.assertIn("REIST_CPP_HOST_OK", self.command([exe]).stdout)
        for mode, status in (("oom", 71), ("pure", 72), ("deleted", 72)):
            with self.subTest(mode=mode):
                self.assertEqual(self.command([exe, mode], success=False).returncode, status)
                self.assertIn("REIST_CPP_HOST_OK", self.command([exe]).stdout)

    def test_mixed_c_cpp_link_and_mypr_admission(self):
        # An isolated minimal conventional sysroot, not a repeated full SDK gate.
        lib = self.directory / "usr/lib"
        include = self.directory / "usr/include"
        lib.mkdir(parents=True, exist_ok=True)
        include.mkdir(parents=True, exist_ok=True)
        c = self.directory / "bridge.c"
        c.write_text('#include <stdint.h>\nstruct Wire {uint32_t version, size, value;}; '
                     'int bridge(struct Wire *w) { return w->version==1 && w->size==12 ? (int)w->value : -1; }\n'
                     '_Noreturn void x86os_exit(int status) { (void)status; for (;;) __asm__("ud2"); }\n', encoding="ascii")
        prefix = builder.freestanding_compile_prefix(self.zig)
        startup = lib / "crt0.o"
        core = lib / "bridge.o"
        self.command([*prefix, "-std=c11", "-c", ROOT / "userspace/sdk/crt0.c", "-o", startup])
        self.command([*prefix, "-std=c11", "-c", c, "-o", core])
        archive = lib / "libreistos.a"
        self.command([self.zig, "ar", "rcs", archive, core])
        source = self.directory / "client.cpp"
        source.write_text('#include <x86os.h>\nstruct Wire {uint32_t version, size, value;}; '
                          'static_assert(sizeof(Wire)==12 && __builtin_offsetof(Wire,value)==8); '
                          'extern "C" int bridge(Wire*); '
                          'extern "C" int main(int,char**) { Wire w{1,sizeof(w),42}; return bridge(&w)==42?0:1; }', encoding="ascii")
        output = self.directory / "client.prg"
        with self.assertRaises(ValueError):
            builder.build([source], output, self.zig, runtime_objects=[startup], runtime_libraries=[archive])
        builder.build([source], output, self.zig, runtime_objects=[startup], runtime_libraries=[archive],
                      include_dirs=[ROOT / "userspace/sdk/include"],
                      cpp=True, cache_directory=self.directory / "target-cache")
        self.assertEqual(output.read_bytes()[:4], b"MYPR")
        # Forbidden archive members are rejected even when no symbol references
        # them and --gc-sections would remove their initialization machinery.
        result, forbidden = self.compile('extern int f(); int v=f();', "discarded.cpp", profile=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        bad = self.directory / "libbad.a"
        self.command([self.zig, "ar", "rcs", bad, forbidden])
        with self.assertRaises(ValueError):
            builder.build([source], output, self.zig, runtime_objects=[startup], runtime_libraries=[archive],
                          include_dirs=[ROOT / "userspace/sdk/include"],
                          libraries=[bad], cpp=True, cache_directory=self.directory / "target-cache")


if __name__ == "__main__":
    unittest.main()
