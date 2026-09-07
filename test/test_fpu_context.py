"""Actual i386 switch behavior and bounded CPU-profile admission."""
import os
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs


class FpuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory = ROOT / "build/codex-agent/r13-fpu" / ("host-" + uuid.uuid4().hex)
        cls.directory.mkdir(parents=True)
        cls.env = os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/browser-host/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"] = str(cls.directory / "zig-local")

    def command(self, args, timeout=60):
        result = subprocess.run([str(x) for x in args], cwd=ROOT, env=self.env,
                                capture_output=True, text=True, timeout=timeout,
                                creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_actual_i386_switch_o0_o2(self):
        # Only object-format C symbol decoration differs on Windows. All
        # instructions and labels inside the real switch remain unchanged.
        assembly = (ROOT / "kernel/sched/switch.asm").read_text()
        assembly = assembly.replace("global swtch", "global _swtch").replace("\nswtch:", "\n_swtch:")
        unit = self.directory / "switch.asm"
        unit.write_text(assembly)
        obj = self.directory / "switch.obj"
        self.command(["C:/tools/nasm-3.02/nasm.exe", "-f", "win32", unit, "-o", obj])
        for opt in ("-O0", "-O2"):
            with self.subTest(opt=opt):
                exe = self.directory / ("switch" + opt + ".exe")
                self.command([find_zig(), "cc", "-target", "x86-windows-gnu", opt,
                              "-fno-builtin", "-mno-sse", "-mno-sse2", "-mno-mmx",
                              "-Wall", "-Wextra", "-Werror",
                              ROOT / "test/test_fpu_context_host.c", obj, "-o", exe], 90)
                result = self.command([exe], 10)
                self.assertIn("FPU_SWITCH_OK pairs=128", result.stdout)

    def test_real_boot_policy_o0_o2(self):
        for opt in ("-O0", "-O2"):
            with self.subTest(opt=opt):
                exe = self.directory / ("boot" + opt + ".exe")
                self.command([find_zig(), "cc", opt, "-DREIST_HOST_TEST",
                              "-I", ROOT, "-std=c11", "-Wall", "-Wextra", "-Werror",
                              ROOT / "arch/x86/cpu/fpu.c",
                              ROOT / "test/test_fpu_boot_host.c", "-o", exe], 90)
                self.assertIn("FPU_BOOT_OK cpus=16", self.command([exe],10).stdout)
                self.assertIn("FPU_BOOT_OK cpus=16", self.command([exe,"amd-mm"],10).stdout)

    def test_lifecycle_layout_and_integer_kernel(self):
        header = (ROOT / "kernel/sched/scheduler.h").read_text()
        scheduler = (ROOT / "kernel/sched/scheduler.c").read_text()
        self.assertIn("uint32_t fpu_state[128] __attribute__((aligned(16)))", header)
        self.assertIn("offsetof(context_t,fpu_state)==X86_FPU_CONTEXT_OFFSET", scheduler)
        creation = scheduler[scheduler.index("static int create_task_with_affinity"):
                             scheduler.index("static bool scheduler_affinity_online")]
        self.assertLess(creation.index("memset(task, 0, sizeof(*task))"),
                        creation.index("x86_fpu_state_reset(task->context.fpu_state)"))
        self.assertLess(creation.index("x86_fpu_state_reset"),creation.index("task->status = TASK_READY"))
        self.assertIn("!x86_fpu_cpu_ready(cpu)", creation)
        reap = scheduler[scheduler.index("size_t scheduler_reap_finished_tasks"):
                         scheduler.index("static int create_task_with_affinity")]
        self.assertIn("memset(task, 0, sizeof(*task))",reap)
        asm = (ROOT / "kernel/sched/switch.asm").read_text()
        self.assertLess(asm.index("fxsave [eax + 32]"),asm.index("fxrstor [edx + 32]"))
        self.assertLess(asm.index("fxrstor [edx + 32]"),asm.index("mov esp, [edx + 0]"))
        self.assertIn("jz .load_new_context",asm)
        smp = (ROOT / "arch/x86/cpu/smp.c").read_text()
        entry = smp[smp.index("void x86_smp_ap_entry"):]
        self.assertLess(entry.index("x86_fpu_initialize_cpu()"),entry.index("x86_cpu_local_mark_online"))
        boot = (ROOT / "kernel/init/kernel.c").read_text()
        early = boot[boot.index("static void early_init"):]
        self.assertLess(early.index("x86_fpu_initialize_cpu()"),early.index("irq_enable()"))
        make = (ROOT / "Makefile").read_text()
        cflags = make[make.index("CFLAGS := -m32"):make.index("DEPFLAGS =")]
        for flag in ("-msoft-float","-mno-sse","-mno-sse2","-mno-mmx"):
            self.assertIn(flag,cflags)
        # The new diagnostic is an existing Ring-3 shell-resolvable program.
        self.assertIn("libexec/reist/gtest.prg=$(SYSTEM_PROGRAM_DIR)/GTEST.PRG",make)
        self.assertIn("'libexec/reist/gtest.prg' = 'GTEST.PRG'",
                      (ROOT/"scripts/build-windows.ps1").read_text())

    def test_guest_evidence_rejects_missing_duplicate_and_fault(self):
        from run_qemu_fpu import validate_transcript
        one="\n".join(("FPU_BEGIN","FPU_PREEMPT_OK parent","FPU_PREEMPT_OK child",
                         "Exception: FP (IRQ 16)","Exception: SIMD (IRQ 19)",
                         "Exception: GP (IRQ 13)","FPU_FAULTS_OK mf=144 xm=147 gp=141",
                         "FPU_REUSE_OK","FPU_OK"))+"\n"
        validate_transcript(one*2,1)
        ap="".join(f"REIST_FPU AP_CONTEXT_OK cpu={i}\n" for i in (1,2,3))
        validate_transcript(ap+one*2,4)
        for bad,cpus in ((one,1),(one*3,1),(one*2,4),(ap+one*2,1),
                         (one*2+"PANIC\n",1),(one.replace("IRQ 16","IRQ 19")*2,1),
                         ((ap+one*2).replace("cpu=3","cpu=2"),4)):
            with self.assertRaises(ValueError): validate_transcript(bad,cpus)

    def test_paired_vmware_guard_is_fail_closed(self):
        from run_qemu_fpu import benchmark_rows,compare_benchmarks
        rows="".join(f"| {area} | {name} | {value} {unit} | OK |\n" for area,name,value,unit in (
            ("CPU","Single CPU",4000,"MOp/s"),("CPU","Multi CPU gesamt",4100,"MOp/s"),
            ("CPU","Multi/Single",1.02,"x"),("RAM","Schreiben",16000,"MiB/s"),
            ("RAM","Lesen",16000,"MiB/s"),("HDD","Seq. Schreiben",12800,"KiB/s"),
            ("HDD","Seq. Lesen",42666.66,"KiB/s")))
        parsed=benchmark_rows(rows)
        samples=[{"side":side,"rows":benchmark_rows(rows)} for side in ["before","after"]*3]
        self.assertEqual(compare_benchmarks(samples)["CPU/Single CPU"]["ratio"],1)
        for i in (1,3,5): samples[i]["rows"]["CPU/Single CPU"]["value"]=3800
        compare_benchmarks(samples)
        for i in (1,3,5): samples[i]["rows"]["CPU/Single CPU"]["value"]=3799
        with self.assertRaises(ValueError): compare_benchmarks(samples)
        for invalid in (rows+rows,rows.replace("MiB/s","KiB/s"),rows.replace("4000","0"),""):
            with self.assertRaises(ValueError): benchmark_rows(invalid)
        for invalid in ([],samples[:-1],list(reversed(samples))):
            with self.assertRaises(ValueError): compare_benchmarks(invalid)
        self.assertEqual(parsed["RAM/Lesen"]["value"],16000)

    def test_tcg_partial_proof_cannot_satisfy_hardware_gate(self):
        from run_qemu_fpu import validate_transcript
        tcg="\n".join(("FPU_BEGIN","FPU_PREEMPT_OK parent","FPU_PREEMPT_OK child",
            "Exception: FP (IRQ 16)","Exception: GP (IRQ 13)",
            "FPU_TCG_FAULTS_OK mf=144 gp_align=141 sse=workstation-required",
            "FPU_REUSE_OK","FPU_TCG_OK"))+"\n"
        validate_transcript(tcg*2,1,profile="tcg")
        with self.assertRaises(ValueError): validate_transcript(tcg*2,1)
        for bad in (tcg.replace("IRQ 13","IRQ 19")*2,
                    tcg.replace("FPU_TCG_OK","FPU_OK")*2,
                    tcg*2+"TEST_FAIL FPU\n"):
            with self.assertRaises(ValueError): validate_transcript(bad,1,profile="tcg")
        with self.assertRaises(ValueError): validate_transcript(tcg*2,1,profile="automatic")

    def test_guest_keeps_distinct_real_hardware_and_alignment_faults(self):
        source=(ROOT/"userspace/programs/guest_test.c").read_text()
        child=source[source.index("static int fpu_child("):source.index("static int fpu_spawn(")]
        self.assertIn('text_equal(mode,"fpu-gp-align")',child)
        self.assertIn('"r"(fpu_expected+1)',child)
        self.assertIn('"fxrstor (%0)"',child)
        self.assertIn('uint32_t invalid=0xffffffff;',child)
        self.assertIn('"ldmxcsr %0"',child)
        self.assertIn('tcg_profile && i==3 ? "fpu-gp-align" : modes[i]',source)
        self.assertNotIn('"int $13',child)

    def test_preemption_evidence_published_after_child_reap(self):
        source=(ROOT/"userspace/programs/guest_test.c").read_text()
        work=source[source.index("static int fpu_work("):source.index("static int fpu_child(")]
        self.assertNotIn("FPU_PREEMPT_OK",work)
        parent=source[source.index("static int fpu_main("):source.index("int main(")]
        self.assertLess(parent.index("int reaped=fpu_reap(child,38)"),parent.index("FPU_PREEMPT_OK parent"))
        self.assertLess(parent.index("if(worked || reaped || fpu_same()!=0) return 96"),
                        parent.index("FPU_PREEMPT_OK child"))
        self.assertIn('return fpu_work(202)==0 ? 38 : 92',source)

    def test_unsupported_guest_requires_specific_rejection(self):
        from run_qemu_fpu import validate_rejection
        proof="REIST_FPU UNSUPPORTED cpu=0\nPANIC: Unsupported or inconsistent FPU context profile\n"
        self.assertTrue(validate_rejection(proof))
        self.assertFalse(validate_rejection("PANIC: another kernel failure\n"))
        for bad in (proof+"BOOT_OK",proof+"REIST_FPU READY",proof+"C:\\>"):
            with self.assertRaises(ValueError): validate_rejection(bad)

    def test_unsupported_fixture_retains_integer_isa(self):
        from types import SimpleNamespace
        from run_qemu_fpu import qemu_fpu_command
        for no_apic in (False,True):
            args=SimpleNamespace(qemu=Path("qemu"),image=Path("image"),
                                 smp=1,no_apic=no_apic,unsupported=True)
            command=qemu_fpu_command(args)
            self.assertEqual(command.count("-cpu"),1)
            cpu=command[command.index("-cpu")+1].split(",")
            self.assertEqual(cpu[0],"qemu32")
            self.assertEqual(set(cpu[1:]),{"-sse","-sse2"} | ({"-apic"} if no_apic else set()))
            self.assertIn("-snapshot",command)

    def test_vmware_script_parses_and_rejects_mixed_mode_before_launch(self):
        script=ROOT/"scripts/run_vmware_mouse.ps1"
        parsed=self.command(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",
            "$tokens=$null; $errors=$null; "
            "[void][System.Management.Automation.Language.Parser]::ParseFile("
            "'"+str(script)+"',[ref]$tokens,[ref]$errors); "
            "if ($errors.Count) { throw ($errors | Out-String) }; 'FPU_PS_PARSE_OK'"],15)
        self.assertIn("FPU_PS_PARSE_OK",parsed.stdout)
        for other in ("-Benchmark","-Visible","-DisplayModes"):
            result=subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile",
                "-File",str(script),"-FpuIsolation",other],capture_output=True,text=True,
                timeout=15,creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
            self.assertNotEqual(result.returncode,0)
            self.assertIn("FPU isolation requires an exclusive hidden",result.stderr)

    def test_vmware_preflight_accepts_absent_processes_rejects_present(self):
        from run_qemu_fpu import VMWARE_PREFLIGHT
        self.command(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",
                      VMWARE_PREFLIGHT],15)
        busy=VMWARE_PREFLIGHT.replace("qemu-system-i386",Path(sys.executable).stem)
        result=subprocess.run(["C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile",
            "-Command",busy],capture_output=True,text=True,timeout=15,
            creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
        self.assertNotEqual(result.returncode,0)
        self.assertIn("Concurrent VM/compiler blocks",result.stderr)

    def test_ap_evidence_published_by_bsp_after_reap(self):
        source=(ROOT/"arch/x86/cpu/smp.c").read_text()
        before_bsp=source[:source.index("bool x86_smp_scheduler_probe(void)")]
        self.assertNotIn('printf("REIST_FPU AP_CONTEXT_OK',before_bsp)
        bsp=source[source.index("bool x86_smp_scheduler_probe(void)"):]
        self.assertLess(bsp.index('REIST_SMP REAP_READY'),bsp.index('REIST_FPU AP_CONTEXT_OK'))

    def test_real_powershell_prompt_recovery_is_single_and_preflight_only(self):
        script=str(ROOT/"scripts/run_vmware_mouse.ps1")
        command=("$tokens=$null; $errors=$null; $ast="
            "[System.Management.Automation.Language.Parser]::ParseFile('"+script+"',"
            "[ref]$tokens,[ref]$errors); $fn=$ast.Find({param($node) "
            "$node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and "
            "$node.Name -eq 'Test-FreshShellPromptNeeded'},$true); "
            ". ([scriptblock]::Create($fn.Extent.Text)); "
            "$shellMarker='C:\\>'; $requiredBeforeInput=@('USB ready','SMP ready',$shellMarker); "
            "$corrupt='USB ready SMP ready C:cpu=1\\>'; "
            "if (!(Test-FreshShellPromptNeeded $corrupt $false)) { throw 'missing request' }; "
            "if (Test-FreshShellPromptNeeded $corrupt $true) { throw 'duplicate request' }; "
            "if (Test-FreshShellPromptNeeded ($corrupt+$shellMarker) $false) { throw 'already ready' }; "
            "if (Test-FreshShellPromptNeeded 'USB ready' $false) { throw 'premature request' }; "
            "'FPU_PROMPT_GUARD_OK'")
        self.assertIn('FPU_PROMPT_GUARD_OK',self.command([
            "C:/Program Files/PowerShell/7/pwsh.exe","-NoProfile","-Command",command],15).stdout)

    def test_fpu_startup_uses_the_bounded_fresh_prompt_guard(self):
        script=(ROOT/"scripts/run_vmware_mouse.ps1").read_text()
        self.assertIn('if (($Benchmark -or $FpuIsolation) -and (Test-FreshShellPromptNeeded',script)
        self.assertEqual(script.count("::SendCommand($vncPort, ' ')"),1)


if __name__ == "__main__":
    unittest.main()
