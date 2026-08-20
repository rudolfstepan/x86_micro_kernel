import tempfile
import unittest
import wave
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.run_qemu_pci_audio import (
    AUDIO_TEST_CYCLES, audio_qemu_command, finalize_qemu_wave, validate_wave,
)


class PciAudioRunnerTests(unittest.TestCase):
    def test_runtime_exceeds_service_fault_restart_budget(self):
        self.assertGreater(AUDIO_TEST_CYCLES, 3)

    def test_qemu_configuration_uses_virtual_hda_and_wav_capture(self):
        command = audio_qemu_command(
            Path("qemu-system-i386"), Path("reist.img"), Path("audio.wav"))
        rendered = " ".join(map(str, command))
        self.assertIn("intel-hda,msi=off,debug=1", rendered)
        self.assertIn("hda-output,audiodev=reistaudio,debug=1", rendered)
        self.assertIn("wav,id=reistaudio", rendered)

    def test_wave_validator_rejects_silence_and_accepts_pcm(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.wav"
            with wave.open(str(path), "wb") as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(b"\0" * 64)
            self.assertFalse(validate_wave(path)[0])
            with wave.open(str(path), "wb") as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(b"\x01\0\x01\0" * 16)
            self.assertTrue(validate_wave(path)[0])

    def test_qemu_zero_length_wave_header_is_finalized_narrowly(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.wav"
            with wave.open(str(path), "wb") as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(b"\x01\0\x01\0" * 16)
            contents = bytearray(path.read_bytes())
            contents[4:8] = b"\0" * 4
            contents[40:44] = b"\0" * 4
            path.write_bytes(contents)
            self.assertTrue(finalize_qemu_wave(path)[0])
            self.assertTrue(validate_wave(path)[0])


if __name__ == "__main__":
    unittest.main()
