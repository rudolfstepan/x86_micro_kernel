import array
import math
import tempfile
import unittest
import wave
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts.run_qemu_pci_audio import (
    AUDIO_TEST_CYCLES, audio_qemu_command, estimate_pitch_hz,
    finalize_qemu_wave, validate_wave,
)


def stereo_tone(frequency: float, frames: int = 48000) -> bytes:
    samples = array.array("h")
    for frame in range(frames):
        sample = int(6000.0 * math.sin(
            2.0 * math.pi * frequency * frame / 48000.0))
        samples.extend((sample, sample))
    return samples.tobytes()


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

    def test_smp_audio_configuration_requires_four_cpus(self):
        command = audio_qemu_command(
            Path("qemu-system-i386"), Path("reist.img"), Path("audio.wav"),
            smp=4)
        rendered = " ".join(map(str, command))
        self.assertIn("-smp 4", rendered)
        runner = (ROOT / "scripts/run_qemu_pci_audio.py").read_text(
            encoding="utf-8")
        self.assertIn("REIST_AUDIO HDA_AP_EXEC cpu=", runner)
        self.assertIn("--expect-hda-smp-restart", runner)
        self.assertIn("REIST_AUDIO HDA_TIMEOUT_ARMED epoch=", runner)
        self.assertIn("wait_for_count(transcript, AUDIO_READY, 2", runner)
        self.assertIn("first bounded request discovers the stale", runner)
        self.assertIn("--expect-audio-service-smp", runner)
        self.assertIn("REIST_AUDIO SERVICE_AP_EXEC cpu=", runner)
        self.assertIn("--expect-audio-service-smp-restart", runner)
        self.assertIn("args.expect_audio_service_smp_restart) and", runner)

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
                output.writeframes(stereo_tone(440.0))
            self.assertTrue(validate_wave(path)[0])

    def test_wave_validator_rejects_wrong_pitch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.wav"
            with wave.open(str(path), "wb") as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(stereo_tone(400.0))
            valid, detail = validate_wave(path)
            self.assertFalse(valid)
            self.assertIn("pitch", detail)

    def test_pitch_estimator_ignores_sparse_hda_ring_artifacts(self):
        mono = array.array("h")
        for frame in range(48000):
            sample = int(6000.0 * math.sin(
                2.0 * math.pi * 440.0 * frame / 48000.0))
            mono.append(sample)
        for boundary in range(2400, len(mono), 2400):
            mono[boundary] = 5440
            mono[boundary + 1] = -220
        estimated = estimate_pitch_hz(mono, 48000)
        self.assertIsNotNone(estimated)
        self.assertGreaterEqual(estimated, 435.0)
        self.assertLessEqual(estimated, 445.0)

    def test_qemu_zero_length_wave_header_is_finalized_narrowly(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.wav"
            with wave.open(str(path), "wb") as output:
                output.setnchannels(2)
                output.setsampwidth(2)
                output.setframerate(48000)
                output.writeframes(stereo_tone(440.0))
            contents = bytearray(path.read_bytes())
            contents[4:8] = b"\0" * 4
            contents[40:44] = b"\0" * 4
            path.write_bytes(contents)
            self.assertTrue(finalize_qemu_wave(path)[0])
            self.assertTrue(validate_wave(path)[0])


if __name__ == "__main__":
    unittest.main()
