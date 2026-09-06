"""Pinned Hubbub numeric-CR adapter: exact patch, drift and archive admission."""
import hashlib
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_html_engine as build


class HtmlEngineBuildTests(unittest.TestCase):
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
