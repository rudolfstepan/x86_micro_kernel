# Audio assets

## System sounds

`startup.wav`, `shutdown.wav`, `error.wav`, `notify.wav`,
`trash-drop.wav` and `trash-empty.wav` are original, procedurally generated
REIST assets. They contain no Microsoft or third-party samples. The deterministic source is
`scripts/generate_system_sounds.py`.

SPDX-License-Identifier: CC0-1.0

To the extent possible under law, the contributors waive all copyright and
related or neighboring rights to these six generated sound files under
CC0 1.0 Universal. The separate `testtone-440hz-mono-48k-s16.wav` fixture
retains its existing provenance.

## Audio test fixture

`testtone-440hz-mono-48k-s16.wav` is the unmodified
`Sine440Hz_1ch48000HzS16.wav` file from
[AkiyukiOkayasu/TestToneSet](https://github.com/AkiyukiOkayasu/TestToneSet).
The upstream repository publishes the test-tone collection under CC0-1.0.

- Format: RIFF/WAVE PCM, mono, signed 16-bit little-endian, 48,000 Hz
- Frames: 240,000 (5 seconds)
- Size: 480,104 bytes
- SHA-256: `0BD661E92A15BF4D3F14385725C42CC03EE14315DCE1FC06A3E735C237799EFA`
- Packaging: host-only parser fixture; it is not installed in the system image

The source asset remains unchanged so host tests can distinguish file parsing
from REIST's built-in signal generator. Runtime playback uses the generated
system sounds instead.
