"""R3.18 real C/C++ differential, profile, stack and frozen paired timing gates."""
import json
import os
import re
import shutil
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_user_program as builder
from measure_cpp_baseline import (compile_mixed_host, git, paired_response_benchmark,
    suppress_windows_test_dialogs, RESPONSE_BASELINE, RESPONSE_C, RESPONSE_CPP, RESPONSE_FLAGS)


def call_graph(disassembly):
    """Read GNU objdump -dr --no-show-raw-insn for this i386 boundary.

    Relocations supersede placeholder branch targets. Tail branches count as
    calls conservatively; intra-function jumps do not create recursion.
    Indirect/unresolved edges are retained and rejected when reachable.
    """
    functions = {}; current = None; pending = None
    for line in disassembly.splitlines():
        start = re.fullmatch(r"[0-9a-f]+ <([^>]+)>:", line.strip())
        if start:
            current = start[1]; functions[current] = []; pending = None
            continue
        relocation = re.match(r"\s*[0-9a-f]+: R_386_PC32\s+(\S+)", line)
        if relocation:
            if pending is not None:
                pending[1] = relocation[1]; pending[2] = True
            continue
        instruction = re.match(r"\s*[0-9a-f]+:\s+(\S+)\s*(.*)", line)
        if not instruction:
            continue
        pending = None
        op, operand = instruction.groups()
        if current is not None and (op.startswith("call") or op.startswith("j")):
            target = re.search(r"<([^>]+)>", operand)
            pending = [op, target[1] if target else "<indirect>", False]
            functions[current].append(pending)
    graph = {}
    for name, branches in functions.items():
        edges = set()
        for op, target, relocated in branches:
            base = target.split("+0x",1)[0]
            if op.startswith("j") and base == name and not relocated:
                continue
            # A call into the middle of a function is not an admitted edge.
            if "+0x" in target:
                raise ValueError("unresolved/interior call: " + target)
            edges.add(target)
        graph[name] = sorted(edges)
    return graph


def stack_paths(frames, graph, root, external):
    """All bounded direct paths to leaves; no unknown/recursive zero-cost edge."""
    def visit(name, ancestors, used):
        if name in ancestors or len(ancestors)>64:
            raise ValueError("recursive/unbounded call graph")
        if name in external:
            return [(name,used)]
        if name not in frames or name not in graph:
            raise ValueError("missing stack/call evidence: " + name)
        used += frames[name]
        if not graph[name]:
            return [(name,used)]
        paths = []
        for callee in graph[name]:
            paths.extend(visit(callee,ancestors+(name,),used))
            if len(paths)>4096:
                raise ValueError("call path capacity")
        return paths
    return visit(root,(),0)


class BrowserResponseCppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory = ROOT / "build/codex-agent/r318/stack-repair/response"
        cls.directory.mkdir(parents=True, exist_ok=True)
        cls.baseline = cls.directory / "oracle.c"
        cls.baseline.write_bytes(git("show", RESPONSE_BASELINE + ":" + RESPONSE_C))
        cls.env = os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/r318/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/r318/zig-local")

    def test_differential_real_code_and_checked_typed_results(self):
        renames = ["-Dbrowser_response_open=baseline_open",
                   "-Dbrowser_response_open_kind=baseline_open_kind",
                   "-Dbrowser_response_open_document=baseline_open_document"]
        for optimization in ("-O0", "-O2"):
            exe = self.directory / ("behavior" + optimization + ".exe")
            compile_mixed_host(["test/test_browser_response_cpp_host.cpp", RESPONSE_CPP, self.baseline,
                               "userspace/programs/curl_http.c", "userspace/gui/lib/html_document.c"],
                              exe, [*RESPONSE_FLAGS, optimization], {str(self.baseline): renames}, self.env)
            result = subprocess.run([str(exe)], cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("BROWSER_RESPONSE_CPP_OK", result.stdout)
            print(optimization, result.stdout.strip())

    def test_success_cannot_be_forged_or_borrowed_from_temporary(self):
        cases = ["ValidatedResponse v;", "browser_response_t m{}; ValidatedResponse v(m,0);",
                 "browser_response_t m{}; ValidatedResponse v({},m,0);",
                 "auto p=ValidatedResponse::open(nullptr,0,nullptr,0).value_if(); (void)p;",
                 "void use(ValidatedResponse v) { (void)reist::move(v).metadata(); }"]
        with tempfile.TemporaryDirectory() as tmp:
            for index, case in enumerate(cases):
                source = Path(tmp) / "reject.cpp"
                source.write_text('#include "userspace/gui/apps/browser/browser_response.hpp"\n'
                                  'using reist::browser::ValidatedResponse;\n' +
                                  (case if index==4 else 'void f(){'+case+'}\n'), encoding="ascii")
                result = subprocess.run([str(builder.find_zig()), "cc", *builder.cpp_compile_flags(),
                    *RESPONSE_FLAGS, "-Iuserspace/cpp/include", "-c", str(source), "-o", str(Path(tmp)/"reject.o")],
                    cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0, case)
                self.assertIn("error:", result.stderr)
                self.assertNotIn("file not found", result.stderr)

    def test_i386_profile_heap_freedom_and_stack_delta(self):
        evidence = {}
        objdump = shutil.which("objdump") or r"C:\msys64\mingw64\bin\objdump.exe"
        roots = ["browser_response_open", "browser_response_open_kind", "browser_response_open_document"]
        for name, source in (("c",self.baseline),("cpp",ROOT/RESPONSE_CPP)):
            obj = self.directory / ("target-"+name+".o")
            flags = builder.cpp_compile_flags() if name=="cpp" else ["-std=c11"]
            subprocess.run([*builder.freestanding_compile_prefix(builder.find_zig()), *flags,
                "-Iuserspace/cpp/include", "-Iuserspace/gui/include", "-Iuserspace/gui/apps/browser",
                "-Xclang", "-stack-usage-file", "-Xclang", str(obj.with_suffix(".su")),
                "-c", str(source), "-o", str(obj)], cwd=ROOT, env=self.env, check=True, timeout=60)
            data = obj.read_bytes()
            builder.validate_cpp_object(data)
            offset = struct.unpack_from("<I",data,32)[0]
            width,count = struct.unpack_from("<HH",data,46)
            sections = [struct.unpack_from("<10I",data,offset+i*width) for i in range(count)]
            undefined = set()
            for section in sections:
                if section[1]!=2:
                    continue
                strings_section = sections[section[6]]
                strings = data[strings_section[4]:strings_section[4]+strings_section[5]]
                for pos in range(section[4],section[4]+section[5],16):
                    symbol,_,_,info,_,index = struct.unpack_from("<IIIBBH",data,pos)
                    if not index and symbol and info>>4:
                        undefined.add(strings[symbol:strings.index(0,symbol)].decode("ascii"))
            self.assertLessEqual(undefined,{"memcpy","memset","memmove","reist_curl_find_header_end",
                "reist_curl_parse_response_head","reist_html_url_resolve_wide"})
            rows = [line.split("\t") for line in obj.with_suffix(".su").read_text().splitlines()]
            self.assertTrue(rows)
            self.assertTrue(all(row[2]=="static" for row in rows),rows)
            assembly = subprocess.check_output([objdump,"-dr","--no-show-raw-insn",str(obj)],
                                               text=True,timeout=15)
            obj.with_suffix(".dis").write_text(assembly,encoding="utf-8")
            frames = {row[0].rsplit(":",1)[1]:int(row[1]) for row in rows}
            graph = call_graph(assembly)
            self.assertEqual(set(frames),set(graph))
            paths = {root:stack_paths(frames,graph,root,undefined) for root in roots}
            evidence[name] = {"stack_rows": rows,"undefined": sorted(undefined),
                              "call_graph":graph,"paths":paths,
                              "boundary_peak":max(size for values in paths.values() for _,size in values)}
        # Compare the maximum C++ prefix to the MINIMUM C prefix at each shared
        # external leaf: unchanged C callee stacks cancel even on different
        # paths. Internal leaves/new memcpy are conservatively charged from
        # zero baseline. The latter's actual leaf frame is measured below.
        memory = self.directory / "bytes.o"
        subprocess.run([*builder.freestanding_compile_prefix(builder.find_zig()), "-std=c11",
            "-Iuserspace/libc/include", "-Xclang", "-stack-usage-file", "-Xclang",str(memory.with_suffix(".su")),
            "-c","userspace/libc/lib/bytes.c","-o",str(memory)],cwd=ROOT,env=self.env,check=True,timeout=60)
        memory_rows = [line.split("\t") for line in memory.with_suffix(".su").read_text().splitlines()]
        copy_frame = next(row for row in memory_rows if row[0].endswith(":memcpy"))
        self.assertEqual(copy_frame[2],"static")
        memory_graph = call_graph(subprocess.check_output([objdump,"-dr","--no-show-raw-insn",str(memory)],text=True,timeout=15))
        self.assertEqual(memory_graph["memcpy"],[]) # No hidden callee or allocation.
        differences = []
        for root in roots:
            for leaf, cost in evidence["cpp"]["paths"][root]:
                baseline = [n for target,n in evidence["c"]["paths"][root] if target==leaf]
                if leaf in evidence["cpp"]["undefined"] and leaf not in evidence["c"]["undefined"]:
                    self.assertEqual(leaf,"memcpy")
                    cost += int(copy_frame[1])
                differences.append(cost-min(baseline) if baseline else cost)
        delta = max(differences)
        evidence["memcpy_leaf_frame"] = copy_frame
        evidence["conservative_additional_stack_bytes"] = delta
        evidence["limit_bytes"] = 32768
        (self.directory/"stack-profile.json").write_text(json.dumps(evidence,indent=2)+"\n")
        self.assertLessEqual(delta,32768)
        # Guest regression: the real process has a guarded 32-KiB TOTAL stack.
        # A +32-KiB module budget alone cannot protect the unchanged C callers.
        # Keep admission's peak near its accepted C predecessor (small call ABI
        # overhead only), not another pair of 8-KiB temporary values on stack.
        self.assertLessEqual(evidence["cpp"]["boundary_peak"],evidence["c"]["boundary_peak"]+256)
        print("conservative additional response stack:",delta)

    def test_stack_accounting_alternatives_recursion_relocations_and_unknowns(self):
        fixture = '''00000000 <a>:
  0: call 1 <a+0x1>
      1: R_386_PC32 common
  5: jne 9 <a+0x9>
00000010 <b>:
 10: jmp 20 <common>
00000020 <common>:
 20: ret
'''
        graph = call_graph(fixture)
        self.assertEqual(graph,{"a":["common"],"b":["common"],"common":[]})
        frames = {"a":8232,"b":8232,"common":16616}
        self.assertEqual(stack_paths(frames,graph,"a",set()),[("common",24848)])
        self.assertEqual(stack_paths(frames,graph,"b",set()),[("common",24848)])
        for bad in ({"a":["a"]},{"a":["missing"]},{"a":["<indirect>"]}):
            with self.assertRaises(ValueError):
                stack_paths(frames,bad,"a",set())
        with self.assertRaises(ValueError):
            call_graph("00000000 <a>:\n 0: call 1 <a+0x1>\n")

    def test_paired_response_timing(self):
        result = paired_response_benchmark(self.directory / "paired")
        print("paired response median ns:",result["median_c_ns"],result["median_cpp_ns"],"ratio",result["ratio"])

    def test_production_build_and_c_callers_use_the_single_cpp_boundary(self):
        source = (ROOT/"scripts/build_system_programs.py").read_text()
        self.assertIn('ROOT / "'+RESPONSE_CPP+'"',source)
        self.assertNotIn('ROOT / "'+RESPONSE_C+'"',source)
        self.assertFalse((ROOT/RESPONSE_C).exists())
        self.assertIn('cpp=name in ("CPPTEST.PRG", "BROWSER.PRG")',source)
        main = (ROOT/"userspace/gui/apps/browser/main.c").read_text()
        fetch = main.split("static int finish_fetch(",1)[1].split("static int navigate(",1)[0]
        self.assertIn("browser_response_open_document(",fetch)
        self.assertIn("browser_response_open_kind(",fetch)
        self.assertLess(fetch.index("if (result < 0) return result;"),fetch.index("response.body_length > limit"))


if __name__ == "__main__":
    unittest.main()
