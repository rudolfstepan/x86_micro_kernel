"""Execute the production private-memory/heap code with faultable host backends."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_user_program import find_zig
from test_memory_r12 import function_block


class ProcessMemoryTests(unittest.TestCase):
    def test_windows_guest_timer_policy_is_per_process_and_verified(self):
        import ctypes
        import run_qemu_smoke as smoke
        for failure in (None, "query", "set", "readback", "ignored"):
            with self.subTest(failure=failure):
                state = {"queries": 0, "value": (1, 1, 1)}
                def query(handle, kind, pointer, size):
                    self.assertEqual((handle, kind, size), (42, 4, 12))
                    state["queries"] += 1
                    if failure=="query" or (failure=="readback" and state["queries"]==2):
                        return 0
                    target=pointer._obj
                    target.Version, target.ControlMask, target.StateMask = state["value"]
                    if failure=="ignored" and state["queries"]==2:
                        target.StateMask |= 4
                    return 1
                def update(handle, kind, pointer, size):
                    self.assertEqual((handle, kind, size), (42, 4, 12))
                    value=pointer._obj
                    state["value"]=(value.Version, value.ControlMask, value.StateMask)
                    self.assertEqual(state["value"], (1,5,1)) # retain unrelated EcoQoS policy
                    return 0 if failure=="set" else 1
                api=SimpleNamespace(GetProcessInformation=mock.Mock(side_effect=query),
                                    SetProcessInformation=mock.Mock(side_effect=update))
                with mock.patch.object(smoke.sys,"platform","win32"), \
                     mock.patch.object(smoke.sys,"getwindowsversion",return_value=SimpleNamespace(build=22000),create=True), \
                     mock.patch.object(ctypes,"WinDLL",return_value=api,create=True):
                    if failure:
                        with self.assertRaises(OSError): smoke.configure_qemu_host_timers(SimpleNamespace(_handle=42))
                    else:
                        self.assertTrue(smoke.configure_qemu_host_timers(SimpleNamespace(_handle=42)))
                        self.assertEqual(state["queries"],2)

    def test_guest_timer_policy_leaves_other_platforms_untouched(self):
        import run_qemu_smoke as smoke
        with mock.patch.object(smoke.sys,"platform","linux"):
            self.assertFalse(smoke.configure_qemu_host_timers(object()))
        with mock.patch.object(smoke.sys,"platform","win32"), \
             mock.patch.object(smoke.sys,"getwindowsversion",return_value=SimpleNamespace(build=19045),create=True):
            self.assertFalse(smoke.configure_qemu_host_timers(object()))

    def test_guest_timer_setup_failure_reaps_the_new_child(self):
        import run_qemu_smoke as smoke
        child=mock.Mock()
        child.poll.return_value=None
        with mock.patch.object(smoke.subprocess,"Popen",return_value=child), \
             mock.patch.object(smoke,"configure_qemu_host_timers",side_effect=OSError("timer policy")):
            with self.assertRaisesRegex(OSError,"timer policy"):
                smoke.run(Path("qemu.exe"),Path("disk.img"),180)
        child.terminate.assert_called_once_with()
        child.wait.assert_called_once_with(timeout=3)
        child.stdin.close.assert_called_once_with()
        child.stdout.close.assert_called_once_with()

    def test_memory_mode_keeps_graphics_prerequisite_and_original_gates(self):
        runner = (ROOT / "scripts/run_qemu_smoke.py").read_text()
        self.assertIn("vmware_vga or expect_process_memory", runner)
        self.assertIn('inject_ps2_command(process, "memtest")', runner)
        self.assertIn('"PRIVATE_MEMORY_RUNTIME_FAIL" in section', runner)
        package = (ROOT / "automation/reist-s03b.toml").read_text()
        self.assertIn("--memory 1024M --expect-process-memory --timeout 180", package)
        self.assertIn("--memory 128M --expect-process-memory --timeout 180", package)
        self.assertIn("-Mode memory-resilience -Target qemu -Video vga", package)

    def build_run(self, fixture, production, extra=()):
        with tempfile.TemporaryDirectory(prefix="reist-memory-host-") as tmp:
            folder = Path(tmp)
            source = folder / "host.c"
            fixture_text = fixture if "\n" in fixture else (ROOT / fixture).read_text()
            source.write_text(fixture_text.replace(
                "/* PRODUCTION */", production), encoding="utf-8")
            executable = folder / "host.exe"
            env = os.environ.copy()
            env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/private-memory/zig-global")
            env["ZIG_LOCAL_CACHE_DIR"] = str(folder / "cache")
            compiled = subprocess.run([str(find_zig()), "cc", "-std=c11", "-O1", "-fno-builtin",
                "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-UNDEBUG", *extra,
                str(source), "-o", str(executable)], cwd=ROOT, env=env,
                timeout=60, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], cwd=ROOT,
                capture_output=True, text=True, timeout=40)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PRIVATE_MEMORY_HOST_OK", result.stdout)

    def test_kernel_allocation_lifetime(self):
        module = ROOT / "kernel/proc/user_memory.c"
        code = "\n".join(line for line in module.read_text().splitlines()
                         if not line.startswith("#include"))
        paging = (ROOT / "arch/x86/mm/paging.c").read_text()
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text()
        code += "\nbool free_page_directory_step(page_directory_t *pd, uint32_t *cursor) " + function_block(
            paging, "bool free_page_directory_step(")
        code += "\nsize_t scheduler_reap_finished_tasks(void) " + function_block(
            scheduler, "size_t scheduler_reap_finished_tasks(")
        self.build_run("test/test_process_memory_host.c", code)

    def test_guest_heartbeat_really_polls(self):
        guest = (ROOT / "userspace/programs/memtest.c").read_text()
        code = "static int try_receive(x86os_ipc_handle_t endpoint,memory_message_t *message) " + function_block(
            guest, "static int try_receive(")
        self.build_run('''#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define X86OS_IPC_MESSAGE_VERSION 1U
typedef uint32_t x86os_ipc_handle_t;
typedef struct { uint32_t type,a,b; } memory_message_t;
typedef struct { uint32_t version,struct_size,length; unsigned char payload[128]; } x86os_ipc_message_t;
static int calls,queued;
static int x86os_ipc_receive_timeout(x86os_ipc_handle_t endpoint,
                                    x86os_ipc_message_t *wire,uint32_t timeout) {
    if (endpoint!=42 || timeout) return -22;
    ++calls;
    if (!queued) return -11;
    memory_message_t value={5,17,3};
    wire->length=sizeof(value); memcpy(wire->payload,&value,sizeof(value));
    return 0;
}
/* PRODUCTION */
int main(void) {
    memory_message_t message={0};
    if (try_receive(42,&message)!=-11 || calls!=1) return 1;
    queued=1;
    if (try_receive(42,&message) || message.type!=5 || message.a!=17 || message.b!=3 || calls!=2) return 2;
    puts("PRIVATE_MEMORY_HOST_OK"); return 0;
}''', code)

    def test_growing_heap_and_automatic_region_return(self):
        code = "\n".join(line for line in (ROOT / "userspace/libc/lib/heap.c").read_text().splitlines()
                         if not line.startswith("#include"))
        self.build_run("test/test_process_heap_host.c", code, (
            "-I"+str(ROOT / "userspace/libc/include"),
            "-Dmalloc=reist_test_malloc", "-Dcalloc=reist_test_calloc",
            "-Drealloc=reist_test_realloc", "-Dfree=reist_test_free"))

    def test_sdk_accepts_the_complete_user_pointer_window(self):
        sdk = (ROOT / "userspace/sdk/x86os.c").read_text()
        code = "void *x86os_malloc(size_t size) " + function_block(sdk, "void* x86os_malloc(")
        code += "\nvoid *x86os_realloc(void *pointer, size_t size) " + function_block(sdk, "void* x86os_realloc(")
        self.build_run('''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define X86OS_SYS_MALLOC 4
#define X86OS_SYS_REALLOC 6
static uintptr_t result;
static uintptr_t x86os_syscall(unsigned n,uintptr_t a,uintptr_t b,uintptr_t c) {
    (void)n;(void)a;(void)b;(void)c;return result;
}
/* PRODUCTION */
int main(void) {
    const uint32_t good[]={0x40800000U,0x80000000U,0x81000000U,0xbefff000U};
    for (unsigned i=0;i<4;++i) {
        result=good[i];
        if ((uintptr_t)x86os_malloc(4096)!=result || (uintptr_t)x86os_realloc((void *)1,8192)!=result) return 1;
    }
    const uint32_t bad[]={0,(uint32_t)-1,(uint32_t)-12,(uint32_t)-13,(uint32_t)-4095};
    for (unsigned i=0;i<5;++i) { result=bad[i]; if (x86os_malloc(4096) || x86os_realloc((void *)1,8192)) return 2; }
    puts("PRIVATE_MEMORY_HOST_OK");return 0;
}''', code)

    def test_process_backing_release_fails_closed(self):
        adapter = (ROOT / "userspace/libc/lib/process_heap.c").read_text()
        code = "static void process_release(void *context, void *storage, size_t capacity) " + function_block(
            adapter, "static void process_release(")
        self.build_run('''#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#define X86OS_SYS_FREE 5U
#define REIST_LIBC_FAULT_HEAP 1U
static uintptr_t result;
static int calls;
static jmp_buf failure;
static uintptr_t x86os_syscall(uint32_t call,uintptr_t pointer,uintptr_t b,uintptr_t c) {
    if (call!=5 || pointer!=0x40800000U || b || c) return (uintptr_t)-22;
    ++calls; return result;
}
static _Noreturn void reist_libc_fail(uint32_t fault) { longjmp(failure,(int)fault); }
/* PRODUCTION */
int main(void) {
    if (setjmp(failure)) return 1;
    process_release(NULL,(void *)0x40800000U,4096);
    if (calls!=1) return 2;
    result=(uintptr_t)-22;
    if (!setjmp(failure)) { process_release(NULL,(void *)0x40800000U,4096); return 3; }
    if (calls!=2) return 4;
    puts("PRIVATE_MEMORY_HOST_OK"); return 0;
}''', code)

    def test_real_frame_admission_preserves_recovery_reserve(self):
        memory = (ROOT / "mm/kmalloc.c").read_text()
        code = "static size_t allocate_frame_from(size_t minimum_address, bool report_failure, bool user_admission) " + function_block(
            memory, "static size_t allocate_frame_from(")
        self.build_run('''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#define FRAME_SIZE 4096U
#define ALIGN_UP(v,a) (((v)+(a)-1)&~((a)-1))
#define REIST_MEMORY_FAULT_INJECTION 1
static int frame_lock;
static bool memory_initialized=true, used[64], frame_fault_armed;
static uint32_t frame_fault_countdown;
static size_t frame_count=64, managed_frame_count=64, free_frame_count=60;
static size_t frame_search_hint=1,allocated_frame_count,allocated_frame_high_water_count;
static uint64_t frame_allocation_failures;
static unsigned lock_depth;
static uint32_t spinlock_acquire_irq(int *lock) { (void)lock;++lock_depth;return 1; }
static void spinlock_release_irq(int *lock,uint32_t flags) { (void)lock;(void)flags;--lock_depth; }
static bool test_frame(size_t i) { return used[i]; }
static void set_frame(size_t i) { used[i]=true; }
/* PRODUCTION */
int main(void) {
    for (unsigned i=0;i<4;++i) used[i]=true;
    frame_fault_armed=true; frame_fault_countdown=0;
    if (allocate_frame_from(FRAME_SIZE,false,true) || free_frame_count!=60 || lock_depth) return 1;
    frame_fault_armed=false;
    for (unsigned i=0;i<56;++i) if (allocate_frame_from(FRAME_SIZE,false,true)<4*FRAME_SIZE) return 2;
    if (free_frame_count!=4 || allocated_frame_count!=56 || allocate_frame_from(FRAME_SIZE,false,true)) return 3;
    if (frame_allocation_failures!=2 || lock_depth) return 4;
    if (!allocate_frame_from(FRAME_SIZE,false,false) || free_frame_count!=3) return 5;
    for (unsigned i=0;i<4;++i) if (!used[i]) return 6;
    puts("PRIVATE_MEMORY_HOST_OK");return 0;
}''', code)


if __name__ == "__main__":
    unittest.main()
