# Startup display settings (R3.21)

Defined 2026-09-07 on accepted `732b2930`; R3.21 accepted through its frozen gates.
The executable scope and frozen gates are in `automation/reist-s03b.toml`.
The user-authorized linker/boot reservation extension passes real-code host
tests and both final reference builds. Standard-VGA and emulated VMware-SVGA
guests prove persisted 800x600/1280x720 restarts, exact scanout/list pixels,
applet fault/replacement, unsupported-mode fallback and shell return. The
128-MiB boot and existing browser-input regression also pass. Earlier failures
and their focused repairs remain documented in CURRENT_WORK; no failed evidence
was relabeled as passing. Native monitor/VMware Workstation mode qualification
and live switching remain outside this acceptance.

References: [QEMU Standard VGA](https://www.qemu.org/docs/master/specs/standard-vga.html)
for the Bochs DISPI/PCI aperture and the
[VMware SVGA-II register definition](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/vmwgfx/device_include/svga_reg.h)
for WIDTH/HEIGHT/MAX_WIDTH/MAX_HEIGHT, BITS_PER_PIXEL, BYTES_PER_LINE,
FB_START/OFFSET/SIZE. The mode envelope is a versioned REIST adapter, not
DRM, VBE BIOS-call or VMware Tools compatibility. Sizes are pixels and bytes;
XRGB8888 uses 32 storage bits and 24 RGB color bits. Existing VBE boot handoff
validation remains the only authority for a native VBE mode.

Ring 3 owns settings, UI and selection. Ring 0 performs bounded resource
admission, register mediation and publication through the existing display
transaction mutex. Extend existing envelopes append-only, never reinterpret
old ACTIVATE fields or reserved fields. No raw framebuffer, MMIO/PIO, DMA or
driver endpoint is granted to DISPLAY.PRG or Control Panel.

Read-only mode queries expose geometry/capacity/backend metadata, not device
addresses. Their results are hints, not transferable authority: activation
revalidates live identity, geometry and memory limits. A request for a different
mode while a backend is active fails before mutation. Legacy automatic mode
selection stays unchanged. Reconnect must not replace the established session
geometry with a saved setting modified during that session.

Geometry admission checks multiplication overflow, pixel layout, pitch, mapped
scanout range and shadow/staged capacity. Both fixed display buffers may grow
to 16 MiB each; they remain disjoint kernel-owned storage. Surface client buffer
budgets and maximum window dimensions do not grow with the screen. A mode must
not silently disable shadow/staged rendering to fit an insufficient budget.
Failed hardware readback does not publish new geometry. Cleanup disables the
failed candidate before automatic fallback. No unbounded register polling.

The optional `resolution` key is appended to `reist.desktop/1`. Missing means
`auto`; explicit values use ASCII decimal WIDTHxHEIGHT. Full document validation
precedes publication; parsing and configuration file I/O stay Ring 3. Existing
CONFIG.PRG performs the atomic replacement; there is no second writer in the
applet. An invalid/unavailable wish receives one automatic startup fallback and
a bounded diagnosis. Both modes failing uses the existing text/degraded path.

DISPLAY.PRG shows current geometry separately from the saved choice, with
native list/button controllers and mouse/keyboard operation. Saving affects
only the next desktop start. The applet owns at most one CONFIG child, polls
without blocking input, uses a monotone deadline and only reaps its own child.
Failed save/readback does not report success. An applet failure must not stop
the session. Control Panel can request only this fixed applet through its
compositor-authorized exact generation, not arbitrary executable paths or
ambient spawn/device authority. Existing Surface cleanup applies.

No live mode switch, persistent trial record, desktop-theme application,
palette change or additional pixel format is included. Those require separate
contracts, particularly a session-owned test/confirm/timeout rollback before
any live display change. QEMU with an emulated VMware adapter is a protocol
proof, not physical VMware Workstation or monitor qualification.

## Adapter v1 and implementation bounds

### Workstation capacity correction (R3.21a)

SVGA-II `VRAM_SIZE` (register 15) is the device VRAM capacity; `FB_SIZE`
(16) describes the current framebuffer extent and is not a limit on future
modes. The [upstream vmwgfx driver](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/vmwgfx/vmwgfx_drv.c)
uses VRAM_SIZE independently of its PCI aperture. QEMU's legacy VMware
emulation returns identical values for these registers, so it cannot alone
prove this distinction. Real Workstation reported 128-MiB VRAM but only
3.75 MiB for FB_SIZE at initial discovery on 2026-09-07.

Seal 32-bit BAR1/BAR2 geometry at boot using existing bounded PCI probing.
Mode queries do not re-probe PCI or enable the display. Live capacity must
fit the sealed aperture; inconsistent capacity/bases fail closed. Initial
WC-first mapping covers at most the existing 16-MiB shadow budget, not all
advertised VRAM. After enable, validate current FB_SIZE, pitch and offset
against VRAM and the BAR before the existing mapper maps the visible span.
The shadow budget bounds each scene, independently of larger device memory.
Preserve failed-disable latching and generation fencing. No ABI change.

The additional Workstation gate runs only a fresh copied test package,
without visible windows or modification of the user's VM. It must prove
saved 1280x720 and 1920x1080 modes, real render/copy and console return.
Until that gate passes, Workstation correction remains a candidate.

`reist_display_mode_request_t` is exactly 64 bytes. Syscall 109 adds operations
13 (query) and 14 (startup activation). Query input fields other than
version/size/operation are zero. Activation input additionally supplies width,
height and `bpp=32`; backend/capacity/fixed-mode/output/reserved fields are zero.
The kernel rechecks canonical desktop identity for operation 14. SVGA-II uses
existing driver-generation authority, command 8 and IPC operation 6; all old
numbers and message sizes remain unchanged. No public physical address.

Bounds are 800x600 through 4096 per axis, further limited by hardware maxima,
VRAM and the 16-MiB shadow budget. The DISPI adapter rejects widths not divisible
by eight, matching [QEMU register normalization](https://www.qemu.org/docs/master/specs/standard-vga.html)
without silently rounding the stored setting. Native VBE exposes only its
sealed mode. Padded pitch and SVGA framebuffer offset are checked after enable;
scanout/FIFO bases must still match the PCI BARs. Readback/initialization failure
disables the candidate; an unconfirmed disable latches a device fault, prevents
fallback and remains visible to the existing supervisor fence/deactivate path.
No uncertain IPC completion is followed by a different-mode replay.

## Kernel storage envelope

The linker admits the existing BIOS loader interval [1 MiB, 64 MiB), without
increasing either loader's maximum or the unchanged 1-GiB user-address boundary.
The ELF32 layout follows [System V ELF program loading](https://gabi.xinuos.com/elf/07-pheader.html):
the two 16-MiB arrays remain NOBITS storage in PT_LOAD memory, not extra disk
payload. They precede the page-aligned 4-KiB boot guard and 8-KiB stack. Both
`_kernel_end` and the PMM's existing `_stack_end` denote the actual final end;
unused linker headroom is not permanently allocated. The PMM already reserves
all frames below its initial heap end, including these arrays, stack and frame
bitmaps. Firmware reservations override usable memory, and the user-frame
admission keeps its unchanged 1/16 recovery reserve. Kernel mappings remain
supervisor-only and boot/task guards remain non-present.

The old, unused linker-only embedded `.user_*` area at 33 MiB is rejected with
a linker assertion, including subsection variants. No runtime program loader
used that area; separate Ring-3 executables/addresses and ABI remain unchanged.
Real ELF links must prove the arrays, nonoverlap, guard/end symbols and NOBITS
size; overflow and embedded-user input must fail. Real PMM tests at 128/1024 MiB
prove reservation, refusal to free protected frames, firmware exclusions and
the recovery reserve. The existing 128-MiB guest boot is still mandatory.

Surface v6 adds opt-in operation 23, `OPEN_DISPLAY`, without changing its
envelope. Only the bound Control Panel generation is granted this fixed action.
The broker validates the complete zero-reserved request and owned live Surface,
coalesces pending requests per client, and rechecks generation at consumption.
Normal Surface/client quotas and retirement remain unchanged.

The applet has at most 17 choices and one owned CONFIG child, with a 5000-ms
save deadline. An exited child is observed before `wait`; PID ownership stays
with its only parent until reap. Readback, not spawn/exit alone, confirms the
saved value. Shell `display --list` resolves the installed program and reports
read-only adapter information; graphical launch is through Control Panel.

The headless acceptance runner uses `desktop.prg --control-probe`, actual QMP
keyboard/mouse events, serial lifecycle evidence and PPM dimensions/list pixels.
Only that diagnostic desktop supplies `--fault-probe` to its Display child;
Ctrl-G then executes a real Ring-3 `ud2` after any save has completed. Normal
applets have no fault gesture. A fresh applet, retained settings and shell return
are required after the exception; a screenshot alone cannot pass the gate.

Diagnostic selection readiness is emitted only for a changed selection after
the compositor accepts its complete paint. The runner must observe the exact
requested value before transferring focus to Save; QMP key admission alone is
not guest input consumption. Missing readiness fails at the original deadline
without reinjecting keys or saving an unconfirmed choice. Normal applet launches
do not emit selection diagnostics. No input-device ordering guarantee is inferred
from this test handshake across the separate keyboard and mouse queues.
