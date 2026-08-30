import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.generate_release_sbom import generate_sbom
from scripts.validate_release_sbom import validate_sbom
from scripts import generate_release_sbom, validate_release_sbom


class ReleaseSbomTests(unittest.TestCase):
    def fixture(self, directory):
        root = Path(directory)
        programs = root / "build/programs"
        programs.mkdir(parents=True)
        kernel = root / "build/kernel.bin"; kernel.write_bytes(b"kernel")
        signature = root / "build/kernel.bin.sig"; signature.write_bytes(bytes(256))
        image = root / "build/reist-os.img"; image.write_bytes(b"image")
        (programs / "A.PRG").write_bytes(b"a")
        (programs / "B.PRG").write_bytes(b"b")
        output = root / "build/reist-sbom.spdx.json"
        return root, programs, [kernel, signature, image], output

    def test_round_trip_binds_every_live_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root, programs, artifacts, output = self.fixture(directory)
            document = generate_sbom(root, output, artifacts, programs)
            self.assertEqual(validate_sbom(output, root), 5)
            self.assertEqual(len(document["relationships"]), 6)
            for entry in document["files"]:
                self.assertNotIn("fileSize", entry)
                self.assertRegex(entry["comment"], r"^REIST-Artifact-Size: \d+ bytes$")
                self.assertEqual(
                    [item["algorithm"] for item in entry["checksums"]],
                    ["SHA1", "SHA256"],
                )
            self.assertIn("packageVerificationCode", document["packages"][0])

    def test_artifact_and_relationship_drift_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root, programs, artifacts, output = self.fixture(directory)
            generate_sbom(root, output, artifacts, programs)
            artifacts[0].write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "artifact drift"):
                validate_sbom(output, root)
            artifacts[0].write_bytes(b"kernel")
            document = json.loads(output.read_text())
            document["relationships"].pop()
            output.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "relationships"):
                validate_sbom(output, root)

    def test_path_escape_duplicate_and_output_input_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root, programs, artifacts, output = self.fixture(directory)
            outside = root / "outside.bin"; outside.write_bytes(b"x")
            with self.assertRaisesRegex(ValueError, "escapes build root"):
                generate_sbom(root, output, artifacts + [outside], programs)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                generate_sbom(root, output, artifacts + [artifacts[0]], programs)
            output.write_bytes(b"old")
            with self.assertRaisesRegex(ValueError, "include the output"):
                generate_sbom(root, output, artifacts + [output], programs)
            self.assertEqual(output.read_bytes(), b"old")

    def test_file_capacity_is_fixed(self):
        with tempfile.TemporaryDirectory() as directory:
            root, programs, artifacts, output = self.fixture(directory)
            for index in range(160):
                (programs / f"P{index:03d}.PRG").write_bytes(b"x")
            with self.assertRaisesRegex(ValueError, "count"):
                generate_sbom(root, output, artifacts, programs)

    def test_capacity_covers_fixed_native_image_and_remains_bounded(self):
        self.assertEqual(generate_release_sbom.MAX_FILE_SIZE, 512 * 1024 * 1024)
        self.assertEqual(generate_release_sbom.MAX_TOTAL_SIZE, 768 * 1024 * 1024)
        self.assertEqual(validate_release_sbom.MAX_FILE_SIZE, 512 * 1024 * 1024)
        self.assertEqual(validate_release_sbom.MAX_TOTAL_SIZE, 768 * 1024 * 1024)

    def test_validator_is_structurally_independent(self):
        source = Path("scripts/validate_release_sbom.py").read_text("utf-8")
        self.assertNotIn("from scripts.generate_release_sbom", source)
        self.assertNotIn("from generate_release_sbom", source)


if __name__ == "__main__":
    unittest.main()
