# PCI audio work package

This package introduces the first bounded playback path for REIST OS without
granting applications direct device access and without adding a complex HDA
state machine to Ring 0. The resilience and degradation contract is a hard
prerequisite, not a later hardening step.

## Delivery order

- [x] Inventory PCI, IRQ, DMA, process-cleanup and append-only syscall rules.
- [x] Complete the capability-scoped Ring-3 driver-domain foundation for PCI
  function identity, IRQ notification, MMIO/PIO mediation and DMA ownership.
- [x] Prove IOMMU isolation or a fully kernel-owned validated DMA mediator on
  each supported platform; otherwise reject HDA for that assurance profile.
- [x] Run the HDA controller and codec state machine in a supervised Ring-3
  driver process with its own address space, quotas and restart budget.
- [x] Run the PCM policy/service endpoint in a separate supervised Ring-3
  audio service; applications never connect to the device driver directly.
- [x] Bind an Intel High Definition Audio controller only after exact PCI and
  BAR validation by the device-resource mediator.
- [x] Reset the controller and codec through finite waits and disable bus
  mastering on every failed initialization path.
- [x] Publish one generation-scoped `S16_LE`, stereo, 48 kHz playback stream.
- [x] Add versioned audio-service IPC for information, open, write and control;
  keep kernel syscalls generic to capability, IPC and device mediation.
- [x] Add `libreistaudio.a` and install `<reist/audio.h>` in the SDK sysroot.
- [x] Package `audioinfo.prg` and `audiotest.prg` for the Ring-3 shell.
- [x] Package a CC0 PCM fixture and a bounded `wavplay.prg` parser/player for
  an independent, manually audible playback check.
- [x] Configure virtual HDA in VMware and deterministic HDA discovery in QEMU.
- [x] Add host/source tests, build both reference packages and run the audio
  runtime smoke when the emulator is available.
- [x] Document the ABI, ownership, supported format and unsupported features.
- [x] Inject driver crash, hang, stale IRQ and failed self-test and prove the
  microkernel plus unrelated Ring-3 processes retain progress.

## Initial support boundary

The first ABI supports playback only: interleaved signed 16-bit little-endian
stereo at 48 kHz. Applications submit bounded nonblocking blocks. Capture,
MIDI, USB audio, resampling and multi-client software mixing are separate
extensions; their absence is reported explicitly rather than emulated through
an incompatible interface.

PCI multimedia class codes are discovery hints only. The initial hardware
backend accepts Intel High Definition Audio programming interface `04:03:00`.
Codec and stream resources are validated before publication.

Audio is optional by default. Exhausted restart budget isolates the audio
driver and service, revokes their device and stream capabilities, reports
system level `DEGRADED`, and leaves shell, desktop, storage and unrelated
processes running. Automatic and manual restart use the same fenced,
generation-scoped supervisor transaction described in
`docs/architecture/RESILIENCE_AND_DEGRADATION_CONTRACT.md`.

## Verification status

The frozen package builds pass for both QEMU and VMware. The latest PCI-audio
QEMU run produced a validated non-silent stereo S16 capture with 281530 frames,
an estimated fundamental of 440.4 Hz and no interior gap longer than one frame.
It ran five complete open/write/start/stop/close cycles without driver or
service degradation. This exceeds the service fault-restart budget and proves
that normal short-lived clients rotate a clean endpoint generation without
causing degradation, while deactivation quiesces DMA before the pool is
refilled. The shared driver-domain tests cover crash, hang, stale-generation
and failed-self-test containment while unrelated Ring-3 work retains progress.

The VMware boot reached the profile-scoped Legacy-INTx fallback,
`HDA_PROFILE pci=15AD:1977` and `REIST_AUDIO SERVICE_READY` without
`HDA_REJECTED`. The user confirmed clearly audible VMware playback at the
expected volume with the packaged CC0 fixture and `wavplay.prg` on 20 August
2026. Correct volume required bounded traversal of the standard HDA route
`DAC -> mixer/selector -> output pin` and activation of only the selected mixer
input; merely raising the already near-full-scale PCM samples was rejected.
ASUS-board output remains a separate physical-hardware proof.
