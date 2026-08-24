# Video subsystem

## VMware SVGA-II 2D profile

REIST implements the VMware SVGA-II identities `15ad:0405` and `15ad:0710`
as an optional supervised Ring-3 driver. The identifiers follow VMware's
`vm_device_version.h`; FIFO command names and capability bits follow the
upstream SVGA/vmwgfx terminology. This is a bounded 2D research profile, not
an implementation of VMware Tools and not a hardware-qualification claim.

The authority boundary is:

```text
desktop -> versioned IPC -> svga2d-ring3 -> syscall 109
        -> fixed kernel mediator -> SVGA-II FIFO
```

The kernel retains PCI discovery, BAR mapping, framebuffer ownership and a
small packet validator. Ring 3 controls activation policy, capability
selection, completion deadlines and service lifecycle. It receives neither a
raw framebuffer/FIFO mapping nor arbitrary port, MMIO or packet authority.
The mediator accepts only mode activation/deactivation, `RECT_FILL`,
capability-gated `RECT_COPY`, one `BUSY` sample and the existing bounded
`UPDATE` publication. Geometry, arithmetic, FIFO metadata and free space are
checked before the next-command pointer is published.

No DMA is required for this profile: all accepted 2D operations address
device-owned VRAM through fixed FIFO coordinates. GMR, surfaces, 3D and direct
guest-memory DMA remain excluded. A later DMA extension would require either
an IOMMU-backed assignment or a kernel-owned validated DMA pool; execution in
Ring 3 alone is not DMA isolation.

The driver's completion loop sleeps between status samples and ends after
50 ms. Queue pressure returns `EAGAIN`; an absent capability returns
`ENOTSUP`. The compositor then keeps its existing staged shadow-buffer copy.
After a successful synchronous `RECT_COPY`, the kernel still updates shadow
state and omits the CPU scanout copy only when the destination remains one
exact damage rectangle. Merged or overlapping damage safely performs the CPU
copy again.

The endpoint is delegated only to `/usr/gui/bin/desktop.prg`. Device and IPC
authority are bound to process generations. Crash, failed self-test, missed
heartbeat or invalid report enters the normal device-domain sequence:
deactivate display, fence, revoke, reap, reset, restart, self-test and only
then reintegrate. VGA text and serial output remain the rescue paths.

Driver readiness does not imply ownership of the visible display. At boot the
driver activates SVGA only for its bounded `RECT_COPY` self-test, disables the
device again with register readback and publishes `SVGA2D_READY` only after the
VGA console has been restored. A desktop session obtains the driver endpoint
through its canonical executable identity, activates SVGA, and sends
`DEACTIVATE` through the same generation-scoped endpoint on every exit path.
Failed or stale IPC drops the delegated capability and permits at most three
reconnect attempts separated by 50 ms. Thus the shell owns VGA before and
after a desktop session; the background driver remains supervised but idle.

VMware Workstation exposes the tested SVGA-II function without a usable PCI
INTx masking path. The exact `MEDIATED_IO` profile therefore registers as
IRQ-less and never grants an IRQ resource. Bus mastering is still disabled and
verified. Generation recovery may omit unavailable PCI function reset only
after the kernel has deactivated SVGA, explicitly recorded mediated-I/O
quiescence and fenced the old owner. The host regression test rejects this
reset bypass when the quiescence proof is absent.

Automated evidence consists of source/ABI tests, native QEMU and VMware
packages, QEMU `-vga vmware` capability/`RECT_COPY` proof, and the bounded
VMware Workstation serial-marker run. Physical hardware is outside this
profile and is tested manually by the user without changing the assurance
claim.

The automated lifecycle run on 24 August 2026 proves under QEMU `-vga vmware`
the ordered sequence boot self-test activation, `SVGA2D_INACTIVE`, driver
readiness, visible shell, desktop activation and accelerated copy, desktop
deactivation, `DESKTOP_EXIT` and restored shell. VMware Workstation separately
requires successful disable readback before `SVGA2D_READY` and `BOOT_OK`.
