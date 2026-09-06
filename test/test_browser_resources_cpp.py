"""R3.19: real C oracle, checked views, target profile/stack and frozen timing."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import sys
import unittest
import platform

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import build_user_program as builder
from measure_cpp_baseline import compile_mixed_host, git, suppress_windows_test_dialogs
from test_browser_response_cpp import call_graph, stack_paths
BASE="2e17d5fb8eb414d4676d3a5fbd8592df8e5dd195"
C="userspace/gui/apps/browser/browser_resources.c"
CPP="userspace/gui/apps/browser/browser_resources.cpp"
DEPS=["userspace/programs/curl_http.c","userspace/gui/lib/html_document.c"]
FLAGS=["-O2","-UNDEBUG","-fno-builtin","-Wall","-Wextra","-Werror",
       "-I.","-Iuserspace/gui/include","-Iuserspace/gui/apps/browser"]

def undefined_symbols(data):
    offset=struct.unpack_from("<I",data,32)[0]
    width,count=struct.unpack_from("<HH",data,46)
    sections=[struct.unpack_from("<10I",data,offset+i*width) for i in range(count)]
    result=set()
    for section in sections:
        if section[1]!=2: continue
        s=sections[section[6]]; strings=data[s[4]:s[4]+s[5]]
        for pos in range(section[4],section[4]+section[5],16):
            name,_,_,info,_,index=struct.unpack_from("<IIIBBH",data,pos)
            if not index and name and info>>4:
                result.add(strings[name:strings.index(0,name)].decode("ascii"))
    return result

class ResourceCppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        suppress_windows_test_dialogs()
        cls.directory=ROOT/"build/codex-agent/r319/resources"
        cls.directory.mkdir(parents=True,exist_ok=True)
        cls.baseline=cls.directory/"oracle.c"
        data=git("show",BASE+":"+C)
        assert git("rev-parse",BASE+":"+C).strip()==b"551b6cf59349c2bc7d6d6ee6af826b8d41479bb2"
        cls.baseline.write_bytes(data)
        cls.exports=re.findall(r"^(?:int|void) (browser_resource\w+)\(",data.decode(),re.M)
        assert len(cls.exports)==11
        cls.env=os.environ.copy()
        cls.env["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r319/zig-global")
        cls.env["ZIG_LOCAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r319/zig-local")

    def test_differential_and_checked_borrows(self):
        renames=["-D"+name+"=baseline_"+name for name in self.exports]
        for opt in ("-O0","-O2"):
            exe=self.directory/("behavior"+opt+".exe")
            compile_mixed_host(["test/test_browser_resources_cpp_host.cpp",CPP,self.baseline,*DEPS],
                              exe,[*FLAGS,opt],{str(self.baseline):renames},self.env)
            result=subprocess.run([str(exe)],cwd=ROOT,env=self.env,capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertRegex(result.stdout,r"BROWSER_RESOURCES_CPP_OK checks=\d+")
            print(opt,result.stdout.strip())

    def test_unchecked_construction_and_temporary_borrow_are_rejected(self):
        for index,case in enumerate(("ValidatedResources v;", "ValidatedResources v(nullptr);",
            "ValidatedResources v({},nullptr);",
            "auto p=ValidatedResources::open(nullptr,nullptr,7).value_if(); (void)p;",
            "void use(ValidatedResources v) { (void)reist::move(v).snapshot(); }")):
            source=self.directory/("reject"+str(index)+".cpp")
            source.write_text('#include "userspace/gui/apps/browser/browser_resources.hpp"\n'
                'using reist::browser::ValidatedResources;\n'+(case if index==4 else 'void f(){'+case+'}'))
            result=subprocess.run([str(builder.find_zig()),"cc",*builder.cpp_compile_flags(),*FLAGS,
                "-Iuserspace/cpp/include","-c",str(source),"-o",str(source.with_suffix(".o"))],
                cwd=ROOT,env=self.env,capture_output=True,text=True,timeout=30)
            self.assertNotEqual(result.returncode,0,case)
            self.assertIn("error:",result.stderr)
            self.assertNotIn("file not found",result.stderr)

    def test_i386_profile_and_each_entrypoint_stack(self):
        evidence={}
        objdump=shutil.which("objdump") or r"C:\msys64\mingw64\bin\objdump.exe"
        for name,source in (("c",self.baseline),("cpp",ROOT/CPP)):
            obj=self.directory/("target-"+name+".o")
            flags=builder.cpp_compile_flags() if name=="cpp" else ["-std=c11"]
            subprocess.run([*builder.freestanding_compile_prefix(builder.find_zig()),*flags,
                "-Iuserspace/cpp/include","-Iuserspace/libc/include","-Iuserspace/gui/include","-Iuserspace/gui/apps/browser",
                "-Xclang","-stack-usage-file","-Xclang",str(obj.with_suffix(".su")),
                "-c",str(source),"-o",str(obj)],cwd=ROOT,env=self.env,check=True,timeout=60)
            data=obj.read_bytes(); builder.validate_cpp_object(data)
            external=undefined_symbols(data)
            self.assertLessEqual(external,{"memcpy","memmove","memset","memcmp","memchr",
                "reist_html_url_resolve_wide","reist_curl_parse_http_url"})
            rows=[line.split("\t") for line in obj.with_suffix(".su").read_text().splitlines()]
            self.assertTrue(rows); self.assertTrue(all(row[2]=="static" for row in rows))
            frames={row[0].rsplit(":",1)[1]:int(row[1]) for row in rows}
            assembly=subprocess.check_output([objdump,"-dr","--no-show-raw-insn",str(obj)],text=True,timeout=15)
            obj.with_suffix(".dis").write_text(assembly)
            graph=call_graph(assembly); self.assertEqual(set(frames),set(graph))
            paths={root:stack_paths(frames,graph,root,external) for root in self.exports}
            evidence[name]=dict(frames=frames,graph=graph,external=sorted(external),paths=paths,
                peaks={root:max(n for _,n in values) for root,values in paths.items()})
        evidence["limit_delta_bytes"]=256
        (self.directory/"stack-profile.json").write_text(json.dumps(evidence,indent=2))
        self.assertLessEqual(set(evidence["cpp"]["external"]),set(evidence["c"]["external"]))
        for root in self.exports:
            self.assertLessEqual(evidence["cpp"]["peaks"][root],evidence["c"]["peaks"][root]+256,root)
            # Shared unchanged external callee stacks cancel; compare the worst
            # candidate prefix to the minimum C prefix, not just module maxima.
            for leaf,cost in evidence["cpp"]["paths"][root]:
                baseline=[n for target,n in evidence["c"]["paths"][root] if target==leaf]
                if leaf in evidence["cpp"]["external"]:
                    self.assertTrue(baseline,(root,leaf))
                    self.assertLessEqual(cost,min(baseline)+256,(root,leaf))
        print("resource per-entrypoint stack:",evidence["c"]["peaks"],evidence["cpp"]["peaks"])

    def test_paired_frozen_validation_timing(self):
        fixture="test/test_browser_resources_cpp_bench.c"
        for path in DEPS:
            self.assertEqual((ROOT/path).read_bytes().replace(b"\r\n",b"\n"),
                             git("show",BASE+":"+path).replace(b"\r\n",b"\n"))
        executables=[self.directory/"baseline.exe",self.directory/"candidate.exe"]
        for source,exe in zip((self.baseline,CPP),executables):
            compile_mixed_host([fixture,source,*DEPS],exe,FLAGS,environment=self.env)
        pairs=[]
        for _ in range(5):
            pairs.append([json.loads(subprocess.check_output([str(exe)],timeout=15,cwd=ROOT)) for exe in executables])
        medians=[statistics.median(pair[i]["resource_validate_ns"] for pair in pairs) for i in range(2)]
        result=dict(baseline_commit=BASE,baseline_sha256=hashlib.sha256(self.baseline.read_bytes()).hexdigest(),
            source_fixture_sha256_lf={path:hashlib.sha256((ROOT/path).read_bytes().replace(b"\r\n",b"\n")).hexdigest()
                for path in [fixture,CPP,"userspace/gui/apps/browser/browser_resources.h",
                             "userspace/gui/apps/browser/browser_resources.hpp",*DEPS]},
            platform=platform.platform(),machine=platform.machine(),processor=platform.processor(),
            compiler=subprocess.check_output([str(builder.find_zig()),"version"],timeout=10).decode().strip(),
            flags=FLAGS,cpp_flags=builder.cpp_compile_flags(),lto=False,iterations_per_sample=200000,
            process_deadline_seconds=15,clock="QueryPerformanceCounter outside loop",pairs_c_cpp=pairs,
            median_c_ns=medians[0],median_cpp_ns=medians[1],ratio=medians[1]/medians[0],limits=dict(ratio=1.2,cpp_ns=50000))
        (self.directory/"paired-resources.json").write_text(json.dumps(result,indent=2))
        self.assertEqual(os.name,"nt"); self.assertEqual(result["compiler"],"0.16.0")
        self.assertTrue(all(pair[i]["resource_validate_ns"]>0 for pair in pairs for i in (0,1)))
        self.assertLessEqual(result["ratio"],1.2); self.assertLessEqual(medians[1],50000)
        print("paired resource validation ns:",medians,"ratio",result["ratio"])

    def test_real_browser_and_worker_use_the_single_boundary(self):
        source=(ROOT/"scripts/build_system_programs.py").read_text()
        self.assertEqual(source.count('ROOT / "'+CPP+'"'),2)
        self.assertNotIn('ROOT / "'+C+'"',source); self.assertFalse((ROOT/C).exists())
        self.assertIn('cpp=name in ("CPPTEST.PRG", "BROWSER.PRG") or name == "HTMLWORK.PRG"',source)
        for path,symbol in (("main.c","browser_resources_pack("),("html_worker.c","browser_resources_unpack("),
                            ("css_engine.c","browser_resources_validate(")):
            self.assertIn(symbol,(ROOT/"userspace/gui/apps/browser"/path).read_text())

if __name__=="__main__": unittest.main()
