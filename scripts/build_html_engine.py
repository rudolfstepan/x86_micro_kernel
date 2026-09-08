"""Pinned NetSurf parser build. No network fetch, host libc or debug stdio."""
from pathlib import Path, PurePosixPath
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
from concurrent.futures import ThreadPoolExecutor
import libcss_layout_adapter

CSS_PIN = ("0.9.2", "2df215bbec34d51d60c1a04b01b2df4d5d18f510f1f3a7af4b80cddb5671154e")

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    "libparserutils": ("0.2.5", "317ed5c718f17927b5721974bae5de32c3fd6d055db131ad31b4312a032ed139"),
    "libhubbub": ("0.3.8", "8ac1e6f5f3d48c05141d59391719534290c59cd029efc249eb4fdbac102cd5a5"),
}


def patch_hubbub_numeric_cr(data):
    # REIST adapter v1, Hubbub 0.3.8: numeric CR must survive tokenisation.
    # WHATWG parsing.html#numeric-character-reference-end-state; literal input
    # CR/LF preprocessing and textarea API newline normalization are separate.
    # Applied only after the unchanged archive pin; fail closed on source drift.
    before = (b"\t\tif (0x80 <= cp && cp <= 0x9F) {\n"
              b"\t\t\tcp = cp1252Table[cp - 0x80];\n"
              b"\t\t} else if (cp == 0x0D) {\n\t\t\tcp = 0x000A;\n"
              b"\t\t} else if (ctx->match_entity.overflow || \n")
    if data.count(before) != 1:
        raise ValueError("libhubbub: numeric-CR patch context mismatch")
    after = before.replace(b"\t\t} else if (cp == 0x0D) {\n\t\t\tcp = 0x000A;\n", b"")
    return data.replace(before, after, 1)


def patch_hubbub_entity_order(data):
    # Adapter v1: deterministic, prefix-balanced insertion into the unchanged
    # upstream ternary trie. Never depend on Perl hash iteration or seed.
    before = (b"foreach my $key (keys %entities) {\n"
              b"   $trie = insert_node($trie, $key, $entities{$key});\n}\n")
    if data.count(before) != 1:
        raise ValueError("libhubbub: entity-order patch context mismatch")
    after = b"""# REIST entity-order adapter v1: median distinct character per prefix.
my @ordered = sort keys %entities;
sub insert_balanced {
   my ($lo, $hi, $depth) = @_;
   return if $lo >= $hi;
   my @groups = ($lo);
   for (my $i = $lo + 1; $i < $hi; $i++) {
      push @groups, $i if substr($ordered[$i], $depth, 1) ne
                         substr($ordered[$i-1], $depth, 1);
   }
   push @groups, $hi;
   my $middle = int(($#groups - 1) / 2);
   my ($first, $last) = ($groups[$middle], $groups[$middle+1]);
   if (substr($ordered[$first], $depth, 1) eq '') {
      my $key = $ordered[$first];
      $trie = insert_node($trie, $key, $entities{$key});
   } else {
      insert_balanced($first, $last, $depth + 1);
   }
   insert_balanced($lo, $first, $depth);
   insert_balanced($last, $hi, $depth);
}
insert_balanced(0, scalar(@ordered), 0);
"""
    return data.replace(before, after, 1)


def extract(destination, css=False):
    roots = []
    pins = dict(PINS)
    if css:
        pins["libcss"] = CSS_PIN
    for name, (version, digest) in pins.items():
        packed = ROOT / "third_party" / (name + ".tar.gz")
        pin = ROOT / "third_party" / (name + ".sha256")
        if hashlib.sha256(packed.read_bytes()).hexdigest() != digest or pin.read_text().split()[0] != digest:
            raise ValueError(f"{name}: archive pin mismatch")
        root = destination / name
        total = 0
        with tarfile.open(packed, "r:gz") as archive:
            for member in archive:
                parts = PurePosixPath(member.name).parts
                if not parts or parts[0] != f"{name}-{version}" or ".." in parts:
                    raise ValueError("invalid parser archive path")
                relative = PurePosixPath(*parts[1:])
                if member.isdir():
                    continue
                if str(relative) not in ("COPYING", "build/Aliases", "build/Entities", "build/make-aliases.pl", "build/make-entities.pl") and not (
                        parts[1] in ("src", "include") and relative.suffix in (".c", ".h", ".inc", ".gperf", ".gen")):
                    continue
                if not member.isfile() or member.size > 4 * 1024 * 1024:
                    raise ValueError("invalid parser archive member")
                total += member.size
                if total > 8 * 1024 * 1024:
                    raise ValueError("parser archive extraction quota")
                data = archive.extractfile(member).read()
                if name == "libcss":
                    data = libcss_layout_adapter.patch(str(relative), data)
                if name == "libhubbub" and str(relative) == "src/tokeniser/tokeniser.c":
                    data = patch_hubbub_numeric_cr(data)
                if name == "libhubbub" and str(relative) == "build/make-entities.pl":
                    data = patch_hubbub_entity_order(data)
                # All stdio consumers are upstream debug-only code. NDEBUG is
                # mandatory; removing the otherwise unused includes grants no
                # fake FILE/API and changes no parser algorithm or table bytes.
                if relative.suffix in (".c", ".h") and relative.name != "css_property_parser_gen.c":
                    data = data.replace(b"#include <stdio.h>", b"/* REIST: debug stdio excluded (NDEBUG). */")
                    # These libraries use inttypes only for integer typedefs
                    # outside disabled debug printing, not conversion APIs.
                    data = data.replace(b"#include <inttypes.h>", b"#include <stdint.h>")
                    if name == "libcss" and str(relative) == "src/parse/mq.c":
                        # Upstream uses POSIX strncasecmp without its header.
                        data = b"#include <strings.h>\n" + data
                target = root.joinpath(*relative.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        if name == "libcss":
            libcss_layout_adapter.install(root)
        if name != "libcss":
            perl = shutil.which("perl") or "C:/msys64/usr/bin/perl.exe"
            generator = "make-aliases.pl" if name == "libparserutils" else "make-entities.pl"
            subprocess.run([perl, "build/" + generator], cwd=root, check=True, timeout=30)
        else:
            from build_user_program import find_zig
            generator = root / "property-generator.exe"
            environment = os.environ.copy()
            environment["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/codex-agent/css-generator-cache")
            environment["ZIG_LOCAL_CACHE_DIR"] = str(root / "host-cache")
            subprocess.run([str(find_zig()), "cc", "-std=gnu11", "-O1",
                str(root / "src/parse/properties/css_property_parser_gen.c"), "-o", str(generator)],
                env=environment, check=True, timeout=60)
            # Run the unchanged host generator, never compile it into the guest.
            for descriptor in (root / "src/parse/properties/properties.gen").read_text().splitlines():
                if not descriptor or descriptor.startswith("#"):
                    continue
                property_name = descriptor.split(":", 1)[0]
                if not property_name.replace("_", "").isalnum():
                    raise ValueError("invalid CSS property generator key")
                output = root / "src/parse/properties" / ("autogenerated_" + property_name + ".c")
                subprocess.run([str(generator), "-o", str(output), descriptor], check=True, timeout=10)
        if name == "libhubbub":
            gperf = shutil.which("gperf") or "C:/msys64/usr/bin/gperf.exe"
            generated = root / "src/treebuilder/autogenerated-element-type.c"
            subprocess.run([gperf, "--output-file=src/treebuilder/autogenerated-element-type.c",
                            "src/treebuilder/element-type.gperf"], cwd=root, check=True, timeout=30)
            data = generated.read_text().replace("\nconst struct element_type_map", "\nstatic const struct element_type_map")
            data = data.replace("#include <inttypes.h>", "#include <stdint.h>")
            generated.write_text(data)
        roots.append(root)
    return roots


def build(artifacts, zig, incremental):
    from build_user_program import freestanding_compile_prefix
    headers = list((ROOT / "userspace/libc/include").rglob("*.h"))
    dependencies = [Path(__file__), Path(libcss_layout_adapter.__file__),
                    ROOT / "userspace/gui/apps/browser/css_values.hpp", *headers]
    for name in (*PINS, "libcss"):
        dependencies.extend(ROOT / "third_party" / (name + suffix) for suffix in (".tar.gz", ".sha256"))
    outputs = [artifacts.library_dir / (name + ".a") for name in (*PINS, "libcss")]
    if incremental and all(p.is_file() and all(d.stat().st_mtime_ns <= p.stat().st_mtime_ns for d in dependencies) for p in outputs):
        return
    with tempfile.TemporaryDirectory(prefix="reist-html-build-") as temporary:
        temporary = Path(temporary)
        roots = extract(temporary, css=True)
        from build_user_sdk import extract_wapcaplet
        wapcaplet = extract_wapcaplet(temporary / "wapcaplet")
        env = os.environ.copy()
        env["ZIG_GLOBAL_CACHE_DIR"] = str(artifacts.root / "html-cache")
        env["ZIG_LOCAL_CACHE_DIR"] = str(temporary / "cache")
        for root, output in zip(roots, outputs):
            includes = [ROOT / "userspace/libc/include", root / "include", root / "src", roots[0] / "include", wapcaplet / "include"]
            prefix = freestanding_compile_prefix(zig, includes)
            # NetSurf buildsystem/Makefile.clang's required type attribute.
            prefix += ["-D_ALIGNED=__attribute__((aligned))", "-Os",
                       "-ffunction-sections", "-fdata-sections"]
            objects = []
            jobs = []
            for index, source in enumerate(sorted((root / "src").rglob("*.c"))):
                if source.name.startswith("autogenerated-") or source.name == "css_property_parser_gen.c":
                    continue
                obj = temporary / (root.name + str(index) + ".o")
                jobs.append([*prefix, "-DWITHOUT_ICONV_FILTER", "-std=c11", "-c", str(source), "-o", str(obj)])
                objects.append(obj)
            def compile_one(command):
                subprocess.run(command, env=env, check=True, timeout=60)
            # Fixed host build concurrency; archive member order stays stable.
            with ThreadPoolExecutor(max_workers=4) as pool:
                list(pool.map(compile_one, jobs))
            archive = temporary / output.name
            subprocess.run([str(zig), "ar", "rcs", str(archive), *map(str, objects)], env=env, check=True, timeout=30)
            shutil.copy2(archive, output)
            shutil.copytree(root / "include", artifacts.include_dir, dirs_exist_ok=True)
            license_path = artifacts.root / "usr/share/licenses" / root.name / "COPYING"
            license_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / "COPYING", license_path)
            version = CSS_PIN[0] if root.name == "libcss" else PINS[root.name][0]
            dependency = "reist-c" if root == roots[0] else "libparserutils"
            if root.name == "libcss":
                dependency += " libwapcaplet"
            pc = ("prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n"
                  f"Name: {root.name}\nDescription: Bounded REIST NetSurf parser port\nVersion: {version}\n"
                  f"Requires: {dependency}\nCflags: -I${{includedir}}\nLibs: -L${{libdir}} -l{root.name[3:]}\n")
            (artifacts.library_dir / "pkgconfig" / (root.name + ".pc")).write_text(pc, encoding="ascii")
