# Audio test assets

`testtone-440hz-mono-48k-s16.wav` is the unmodified
`Sine440Hz_1ch48000HzS16.wav` file from
[AkiyukiOkayasu/TestToneSet](https://github.com/AkiyukiOkayasu/TestToneSet).
The upstream repository publishes the test-tone collection under CC0-1.0.

- Format: RIFF/WAVE PCM, mono, signed 16-bit little-endian, 48,000 Hz
- Frames: 240,000 (5 seconds)
- Size: 480,104 bytes
- SHA-256: `0BD661E92A15BF4D3F14385725C42CC03EE14315DCE1FC06A3E735C237799EFA`
- Installed path: `/usr/share/sounds/440hz.wav`

The source asset remains unchanged so a test can distinguish file parsing and
playback from REIST's built-in signal generator. `wavplay.prg` converts the
mono samples to the audio ABI's stereo format in bounded Ring-3 storage.
