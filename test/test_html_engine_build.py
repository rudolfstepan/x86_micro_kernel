"""Pinned Hubbub numeric-CR adapter: exact patch, drift and archive admission."""
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_html_engine as build
from build_user_program import find_zig
from measure_cpp_baseline import suppress_windows_test_dialogs


class HtmlEngineBuildTests(unittest.TestCase):
    def test_image_program_paths_use_native_short_aliases(self):
        from verify_text_artifacts import image_program_path,UNCHANGED
        self.assertEqual(image_program_path("BENCHMARK.PRG"),"usr/bin/benchm~1.prg")
        self.assertEqual(image_program_path("BROWSER.PRG"),"usr/gui/bin/browser.prg")
        self.assertEqual(image_program_path("GTEST.PRG"),"libexec/reist/gtest.prg")
        for name in (*UNCHANGED,"HTMLWORK.PRG"):
            for component in image_program_path(name).split("/"):
                stem,_,extension=component.partition(".")
                self.assertLessEqual(len(stem),8)
                self.assertLessEqual(len(extension),3)

    def test_cold_worker_uses_existing_cpp_include_profile(self):
        import inspect
        import verify_text_artifacts
        self.assertIn("base.cpp_include_dir",inspect.getsource(verify_text_artifacts.rebuild))

    def test_artifact_proof_rejects_stale_or_missing_products(self):
        from verify_text_artifacts import verified_worker,read_fat_file
        with mock.patch("verify_text_artifacts.inputs",return_value={"source":"digest"}):
            for report in ({},{"version":1,"passed":True,"inputs":{}},
                           {"version":1,"passed":True,"inputs":{"source":"digest"},"products":[]}):
                with self.assertRaises(ValueError): verified_worker(report)
        with tempfile.TemporaryDirectory(prefix="reist-fat-bounds-") as temporary:
            image=Path(temporary)/"invalid.img"; image.write_bytes(bytes(512))
            with self.assertRaisesRegex(ValueError,"FAT read bounds"):
                read_fat_file(image,"usr/bin/htmlwork.prg")

    def test_entities_are_reproducible_and_balanced(self):
        suppress_windows_test_dialogs()
        with tempfile.TemporaryDirectory(prefix="reist-entities-") as temporary:
            directory=Path(temporary)
            products=[]
            for seed in ("1","23456"):
                with mock.patch.dict(os.environ,{"PERL_HASH_SEED":seed,"PERL_PERTURB_KEYS":"2"}):
                    roots=build.extract(directory/seed)
                products.append((roots[1]/"src/tokeniser/entities.inc").read_bytes())
            self.assertTrue(products[0]==products[1],"entity table depends on Perl hash seed")
            # Golden spellings/values come from the pinned input, not the trie.
            root=roots[1]
            entries=[line.split() for line in (root/"build/Entities").read_text().splitlines()
                     if line.strip() and not line.startswith("#")]
            self.assertGreater(len(entries),2000)
            vectors="\n".join('{'+json.dumps(row[0])+','+row[1]+'},' for row in entries)
            source=(root/"src/tokeniser/entities.c").read_text()
            before="\twhile (p != -1) {"
            self.assertEqual(source.count(before),1)
            source="static unsigned probes;\n"+source.replace(before,before+"\n\t\t++probes;")
            (root/"src/tokeniser/entities-probe.c").write_text(source)
            harness=directory/"entities-test.c"
            harness.write_text('#include <stdio.h>\n#include <stdint.h>\n#include "entities-probe.c"\n'
                'static const struct {const char *key; uint32_t value;} cases[]={\n'+vectors+'\n};\n'
                'int main(void) { unsigned maximum=0;\n'
                ' for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {\n'
                '  int32_t context=-1; uint32_t value=0; int result=-1;\n'
                '  for(const unsigned char *p=(const unsigned char *)cases[i].key;*p;++p) {\n'
                '   probes=0; result=hubbub_entities_search_step(*p,&value,&context);\n'
                '   if(probes>maximum) maximum=probes; if(probes>7) return 2;\n'
                '   if(result!=HUBBUB_OK && result!=HUBBUB_NEEDDATA) return 3;\n'
                '  } if(result!=HUBBUB_OK || value!=cases[i].value) return 4;\n'
                ' }\n'
                ' for(unsigned c=0;c<256;++c) { int32_t ctx=-1; uint32_t value=0;\n'
                '  probes=0; (void)hubbub_entities_search_step(c,&value,&ctx); if(probes>7) return 5; }\n'
                ' if(hubbub_entities_search_step(65,0,0)!=HUBBUB_BADPARM) return 6;\n'
                ' printf("ENTITY_LOOKUP_OK entries=%u max_probes=%u\\n",(unsigned)(sizeof(cases)/sizeof(cases[0])),maximum); return 0; }\n')
            environment=os.environ.copy()
            environment["ZIG_GLOBAL_CACHE_DIR"]=str(ROOT/"build/codex-agent/r322-text/entity-cache")
            environment["ZIG_LOCAL_CACHE_DIR"]=str(directory/"cache")
            for opt in ("-O0","-O2"):
                exe=directory/(opt+".exe")
                command=[str(find_zig()),"cc","-std=c11",opt,"-I"+str(root/"include"),
                    "-I"+str(root/"src"),"-I"+str(root/"src/tokeniser"),str(harness),"-o",str(exe)]
                compiled=subprocess.run(command,env=environment,capture_output=True,text=True,timeout=90,
                    creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
                self.assertEqual(compiled.returncode,0,compiled.stderr)
                result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30,
                    creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn("ENTITY_LOOKUP_OK",result.stdout)
                print(result.stdout.strip())

    def test_entity_adapter_rejects_source_drift(self):
        with tarfile.open(ROOT/"third_party/libhubbub.tar.gz","r:gz") as packed:
            original=packed.extractfile("libhubbub-0.3.8/build/make-entities.pl").read()
        adapted=build.patch_hubbub_entity_order(original)
        for data in (b"",original+original,adapted,original.replace(b"keys %entities",b"sort keys %entities")):
            with self.assertRaisesRegex(ValueError,"entity-order patch context"):
                build.patch_hubbub_entity_order(data)

    def source(self):
        archive = ROOT / "third_party/libhubbub.tar.gz"
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                         "8ac1e6f5f3d48c05141d59391719534290c59cd029efc249eb4fdbac102cd5a5")
        with tarfile.open(archive, "r:gz") as packed:
            return packed.extractfile("libhubbub-0.3.8/src/tokeniser/tokeniser.c").read()

    def test_only_obsolete_numeric_cr_branch_changes(self):
        original = self.source()
        branch = b"\t\t} else if (cp == 0x0D) {\n\t\t\tcp = 0x000A;\n"
        self.assertEqual(original.count(branch), 1)
        self.assertEqual(build.patch_hubbub_numeric_cr(original), original.replace(branch, b""))

    def test_missing_duplicate_changed_and_already_patched_context_rejected(self):
        original = self.source()
        for source in (b"", original + original, original.replace(b"cp = 0x000A;", b"cp = 0xA;"),
                       build.patch_hubbub_numeric_cr(original)):
            with self.subTest(size=len(source)):
                with self.assertRaisesRegex(ValueError, "numeric-CR patch context"):
                    build.patch_hubbub_numeric_cr(source)

    def test_shared_host_guest_extraction_applies_patch(self):
        with tempfile.TemporaryDirectory(prefix="reist-html-patch-") as tmp:
            roots = build.extract(Path(tmp))
            actual = (roots[1] / "src/tokeniser/tokeniser.c").read_bytes()
        expected = build.patch_hubbub_numeric_cr(self.source())
        expected = expected.replace(b"#include <stdio.h>", b"/* REIST: debug stdio excluded (NDEBUG). */")
        expected = expected.replace(b"#include <inttypes.h>", b"#include <stdint.h>")
        self.assertEqual(actual, expected)

    def test_bad_archive_pin_rejected_before_patch(self):
        with tempfile.TemporaryDirectory(prefix="reist-html-pin-") as tmp:
            with mock.patch.dict(build.PINS, {"libhubbub": ("0.3.8", "0" * 64)}, clear=True):
                with mock.patch.object(build, "patch_hubbub_numeric_cr") as patch:
                    with self.assertRaisesRegex(ValueError, "archive pin mismatch"):
                        build.extract(Path(tmp))
                    patch.assert_not_called()
                    self.assertEqual(list(Path(tmp).iterdir()), [])

    def test_bad_sidecar_pin_rejected_before_patch(self):
        with tempfile.TemporaryDirectory(prefix="reist-html-pin-") as tmp:
            with mock.patch.object(Path, "read_text", return_value="0" * 64):
                with mock.patch.object(build, "patch_hubbub_numeric_cr") as patch:
                    with self.assertRaisesRegex(ValueError, "archive pin mismatch"):
                        build.extract(Path(tmp))
                    patch.assert_not_called()
                    self.assertEqual(list(Path(tmp).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
