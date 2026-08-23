import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.sign_boot_artifact import sign_artifact
from scripts.verify_boot_signature import verify_signature


POLICY = ROOT / "safety/boot_trust_policy.json"
PRIVATE_KEY = ROOT / "test/fixtures/reist-research-dev-private.pem"
PUBLIC_KEY = ROOT / "safety/keys/reist-research-dev-public.pem"


class BootSignatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        found = shutil.which("openssl")
        fallback = Path(r"C:\msys64\mingw64\bin\openssl.exe")
        cls.openssl = Path(found) if found else fallback
        if not cls.openssl.is_file():
            raise unittest.SkipTest("OpenSSL is required for RSA-PSS tests")

    def _signed(self, directory: str) -> tuple[Path, Path]:
        artifact = Path(directory) / "kernel.bin"
        signature = Path(directory) / "kernel.bin.sig"
        artifact.write_bytes(bytes((index * 41 + 5) & 0xFF for index in range(8193)))
        digest = sign_artifact(
            artifact, signature, PRIVATE_KEY, POLICY,
            self.openssl, "research",
        )
        self.assertEqual(digest, hashlib.sha256(artifact.read_bytes()).hexdigest())
        self.assertEqual(signature.stat().st_size, 256)
        return artifact, signature

    def test_valid_rsa_pss_signature_matches_pinned_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact, signature = self._signed(directory)
            digest = verify_signature(
                artifact, signature, POLICY, self.openssl, ROOT
            )
            self.assertEqual(digest, hashlib.sha256(artifact.read_bytes()).hexdigest())

    def test_tampered_artifact_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact, signature = self._signed(directory)
            changed = bytearray(artifact.read_bytes())
            changed[len(changed) // 2] ^= 1
            artifact.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, "verification failed"):
                verify_signature(artifact, signature, POLICY, self.openssl, ROOT)

    def test_tampered_signature_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact, signature = self._signed(directory)
            changed = bytearray(signature.read_bytes())
            changed[100] ^= 0x80
            signature.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, "verification failed"):
                verify_signature(artifact, signature, POLICY, self.openssl, ROOT)

    def test_policy_fingerprint_mismatch_fails_before_signature_use(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact, signature = self._signed(directory)
            policy = json.loads(POLICY.read_text(encoding="utf-8"))
            policy["public_key_spki_sha256"] = "00" * 32
            changed_policy = Path(directory) / "policy.json"
            changed_policy.write_text(json.dumps(policy), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                verify_signature(
                    artifact, signature, changed_policy, self.openssl, ROOT
                )

    def test_release_mode_rejects_research_development_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "kernel.bin"
            signature = Path(directory) / "kernel.bin.sig"
            artifact.write_bytes(bytes(4096))
            with self.assertRaisesRegex(ValueError, "external release policy"):
                sign_artifact(
                    artifact, signature, PRIVATE_KEY, POLICY,
                    self.openssl, "release",
                )
            self.assertFalse(signature.exists())

    def test_policy_fingerprint_is_der_subject_public_key_info(self):
        policy = json.loads(POLICY.read_text(encoding="utf-8"))
        result = subprocess.run(
            [str(self.openssl), "pkey", "-pubin", "-in", str(PUBLIC_KEY),
             "-outform", "DER"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(
            hashlib.sha256(result.stdout).hexdigest(),
            policy["public_key_spki_sha256"],
        )
        self.assertTrue(policy["stage2_signature_verification"])

    def test_stage2_modulus_matches_policy_pinned_public_key(self):
        result = subprocess.run(
            [str(self.openssl), "rsa", "-pubin", "-in", str(PUBLIC_KEY),
             "-modulus", "-noout"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        prefix = "Modulus="
        self.assertTrue(result.stdout.strip().startswith(prefix))
        modulus = bytes.fromhex(result.stdout.strip()[len(prefix):])
        stage2 = (ROOT / "arch/x86/boot/bios/stage2_bios.asm").read_text(
            encoding="utf-8"
        )
        encoded = stage2.split("rsa_modulus:", 1)[1].split(
            "rsa_modulus_end:", 1
        )[0]
        little_endian = bytes(
            int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", encoded)
        )
        self.assertEqual(len(little_endian), 256)
        self.assertEqual(little_endian[::-1], modulus)

    def test_stage2_verifies_pss_before_elf_parsing(self):
        stage2 = (ROOT / "arch/x86/boot/bios/stage2_bios.asm").read_text(
            encoding="utf-8"
        )
        self.assertIn("rsa_pss_verify:", stage2)
        self.assertIn("rsa_modulus:", stage2)
        self.assertLess(stage2.index("call rsa_pss_verify"),
                        stage2.index("call parse_elf_header"))
        self.assertIn("Kernel RSA-PSS verification failed", stage2)

    def test_build_paths_sign_then_independently_verify(self):
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        windows = (ROOT / "scripts/build-windows.ps1").read_text(encoding="utf-8")
        for source in (makefile, windows):
            signing = source.index("sign_boot_artifact.py")
            verification = source.index("verify_boot_signature.py")
            image = source.index("create_native_boot_image.py")
            self.assertLess(signing, verification)
            self.assertLess(verification, image)
        verifier = (ROOT / "scripts/verify_boot_signature.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("sign_boot_artifact", verifier)


if __name__ == "__main__":
    unittest.main()
