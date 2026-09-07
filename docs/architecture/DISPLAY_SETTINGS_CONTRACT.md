# Startup display settings (R3.21)

Defined 2026-09-07 on accepted `732b2930`, not yet implemented or qualified.
The executable scope and frozen gates are in `automation/reist-s03b.toml`.

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
applet. A invalid/unavailable wish receives one automatic startup fallback and
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
