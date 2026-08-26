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

## NVIDIA GK208 native 2D bring-up boundary

The physical ASUS target contains NVIDIA PCI function `10de:1280`, identified
by the upstream NVIDIA PCI list and Envytools as GK208/GeForce GT 635.  REIST
binds only that exact `03:00` function to `nvidia-gk208-ring3`; no NVIDIA
family wildcard is used.  The NVIDIA open kernel modules are not a usable
implementation base for this card because their supported hardware begins at
Turing, while GK208 is Kepler.  Register terminology therefore follows the
upstream Nouveau/Envytools model.

The first native gate is deliberately passive.  The generic device-domain
mediator clips the physical 16-MiB BAR0 to an immutable `0x5fa60c`-byte
read-only aperture. `R2.2q` extends the earlier GPCCS-DMEM boundary only far
enough to read `GPC_UNIT(31, 0x2608)` for a fixed-capacity topology snapshot. The
supervised Ring-3 driver reads PMC identity/enable,
the free-running PTIMER and PFIFO/PGRAPH interrupt state through aligned
32-bit Device-Control operations.  Its region descriptor grants no mapping
or write right; Ring 3 receives no directly usable BAR/VRAM mapping, DMA, IRQ,
bus-master or arbitrary command authority.  The
loader-sealed VBE `1024x768x32` scanout remains active and is still restored to
VGA text through the existing validated path.

The driver exports the existing versioned desktop 2D endpoint so activation,
deactivation, generation fencing and software fallback do not fork into a
second compositor protocol.  It advertises zero acceleration capabilities:
`RECT_FILL` and `RECT_COPY` return `ENOTSUP`.  Native acceleration may be
enabled only after the follow-up gate constructs a bounded GK208 GPFIFO
channel and GPU address space, submits only fixed 2D methods, and observes a
real fence before a monotonic deadline.  Until then `NVIDIA_GK208_READY` means
only that exact identity and passive register access passed; it is not an
acceleration claim.

`R2.2g` freezes the command side of that later engine boundary without
submitting it.  A heap-free 64-dword compiler emits exactly one of two
accepted `FERMI_TWOD_A` packet shapes on fixed subchannel 3: pitch-linear
XRGB8888 rectangle fill or overlap-safe same-surface rectangle copy.  Its
second parser verifies every packet opcode, count, subchannel, method, fixed
value, 40-bit aligned surface range, pitch and rectangle before a future
kernel mediator may consume the stream.  The method and DMA-packet terminology
follows the upstream Nouveau `cl902d.h` and `cl906f.h` class headers.

`R2.2h` moves the read-only live engine preflight completely out of
`display_control`.  Ring 0 now validates only exact PCI identity and BAR
geometry; it neither maps nor dereferences NVIDIA registers.  Ring 3 requests
the clipped generation-scoped aperture and obtains two coherent snapshots
separated by one bounded millisecond sleep and
requires stable PMC identity/BAR geometry plus a strictly advancing PTIMER.
PFIFO and PGRAPH interrupt registers are sampled only for diagnostics.  This
does not initialize PGRAPH: GK208 still needs its documented register packs,
FECS/GPCCS firmware and graphics context, GPU virtual memory, one fixed
Kepler GPFIFO channel and a real fence.  Bus mastering, DMA, IRQs, VRAM and
all acceleration capability bits therefore remain disabled in this package.
PTIMER rollover handling is bounded to four high-low-high attempts per
snapshot; failure closes the driver generation instead of polling forever.

`R2.2i` seals the next hardware-effect-free boundary: a fixed 72-dword
submission envelope binds `FERMI_TWOD_A` to subchannel 3, embeds exactly one
already validated fill or copy stream, appends one four-method, wait-for-idle
4-byte semaphore release and describes the result with exactly one Kepler
GPFIFO entry. The entry carries only a nonzero aligned 40-bit GPU address and
the exact dword length; conditional fetch, privileged execution, subroutine
level, sync wait, padding and every cross-field mismatch are rejected by an
independent validator. The packet and fence fields follow NVIDIA's
MIT-licensed `cl906f` class contract and Nouveau's `chan506f` GPFIFO encoding.
The supervised process exercises this only as a software self-test. It does
not allocate GPU addresses, create a channel, write USERD, load firmware or
submit work, so acceleration capabilities remain zero.

`R2.2j` gives the exact GK208 process one kernel-owned 64-KiB mediated-DMA
pool while keeping its BAR aperture read-only. The first 4 KiB remain
kernel-only. Three fixed, non-overlapping windows above it hold one 8-byte
GPFIFO entry, the complete zero-padded 72-dword pushbuffer and one zeroed
4-byte fence. Ring 3 stages and reads back exactly those 300 bytes using
bounded transfers; it receives neither the pool's physical address nor a raw
mapping. The generic mediator now also rejects DMA binding for every profile
that declares `MEDIATED_IO` without `MEDIATED_DMA`, before allocating a pool.
The fixed GPU virtual addresses are only a future page-table placement
contract. No GPU mapping, RAMFC, runlist, DMA-address register, USERD kick,
IRQ, activation or bus mastering exists yet, and capabilities remain zero.

`R2.2k` seals the next hardware-inactive channel-memory boundary from the
upstream Nouveau GK208 FIFO selection. GK208 uses `gk110_chan` and
`gk110_runl`; its single-channel path inherits the GK104 4-KiB instance/RAMFC,
512-byte USERD and 8-byte channel-runlist formats. REIST compiles one
unprivileged channel ID 1 with a 4-KiB GPFIFO at the fixed placement VA,
requires every unspecified word to remain zero and stages the three complete
images in separate aligned DMA-pool windows. The two RAMFC USERD-address words
remain zero. Exactly one symbolic 64-bit relocation names the kernel-owned
USERD window, but this package deliberately does not resolve it or expose a
physical address. The supervised self-test transfers and reads back each image
in bounded chunks. GPU page directories, GR context pointers, channel binding,
runlist commit, USERD PUT, register writes and hardware execution remain absent,
so capabilities are still zero.

`R2.2l` adds the bounded GPU-VM image plan without making it executable. The
exact GK208 profile selects a fixed 512-KiB mediated-DMA pool; all other
profiles keep the 64-KiB capacity. The original command and channel windows
remain below 64 KiB, followed by a 128-KiB page-directory reservation and a
256-KiB page-table reservation. Two independently validated plans implement
Nouveau's GK104/GK208 4-KiB GPU-page geometries: 14+14 index bits for 64-KiB
FB pages and 13+15 bits for 128-KiB FB pages, in both cases covering a 40-bit
GPU VA. Exactly five symbolic relocations name the instance PGD, one PGD/PT
link and NCOH mappings for the read-only pushbuffer, writable fence and
read-only GPFIFO pages. Their destination words stay zero in Ring 3; only the
common `2^40-1` VM limit is staged into the instance image. No physical
address is published or resolved, no page directory is activated and no
hardware register is written.

`R2.2m` resolves the previously symbolic addresses inside the kernel. The
immutable GK208 device profile installs two exact six-rule templates: USERD,
instance-to-PGD, PGD-to-PT and the three data PTEs for either 64-KiB or
128-KiB FB pages. The supervised driver chooses policy 17, matching Nouveau's
`default_bigpage = 17`, after staging and readback are complete. Command 19
first compares every rule with the template and proves all destination words
zero, then commits the complete set and seals the pool. Ring 3 receives only a
status code and cannot read or mutate the resolved image afterward. The
alternative policy 16 remains validated but unselected. Register `0x100c80`,
GPU page-directory activation, runlist/USERD publication and bus mastering
remain untouched by that package.

`R2.2n` now applies the matching framebuffer page mode through append-only
Device Control command 20. The immutable GK208 profile permits only bit 0 of
BAR0 register `0x100c80`: policy 16 sets it and the selected policy 17 clears
it, matching Nouveau's `gf100_fb_init_page`. Device, read-only BAR resource and
sealed DMA resource must belong to one owner generation. Ring 0 preserves and
verifies every other register bit and records the original selected bit. A
failed transaction rolls back immediately. Driver fencing disables bus
mastering before restoring and verifying that original bit; a failed restore
stays fenced and retryable. Ring 3 receives only status and still has no MMIO
write right. The PGD becomes hardware-visible only through the later channel
instance bind and runlist commit, which remain absent together with GR
initialization, USERD kick, IRQs, bus mastering and acceleration capabilities.

`R2.2o` freezes the prerequisite GR firmware input without uploading it. The
exact built-in Nouveau `hubgk208` (FECS) and `gpcgk208` (GPCCS) nofw data/code
images come from pinned Linux commit
`45c13f3f9e3bb15fd89ff2864c6f627a3b4b4229` under its MIT SPDX license. They
occupy 193/640 and 27/384 little-endian dwords respectively. A fixed 64-byte
manifest records those lengths, the four IEEE CRC32 values and the total 1244
dwords. The supervised driver validates all 4976 bytes before opening its DMA
pool; callers can retrieve only one bounds-checked copied word, never a mutable
image pointer. There is no runtime filesystem or network dependency. Firmware
upload, topology-dependent GR register packs, FECS/GPCCS start, readiness
polling, context generation, channel bind and hardware execution remain the
responsibility of later rollback-capable packages.

`R2.2p` completes the whole safe pre-start hardware slice. The four validated
images occupy fixed non-overlapping windows from `0x70000` in the existing
512-KiB mediated pool and are staged and read back before command 19 seals the
pool. Append-only Device Control command 21 then revalidates all four CRCs,
preserves every unrelated `PMC_ENABLE` bit while resetting only GR, and waits
at most 100 monotonic milliseconds for FECS/GPCCS memory scrubbing. It uploads
both DMEM/IMEM pairs using Nouveau's Falcon v1 auto-increment ports, emits one
tag per 256-byte IMEM block and reads every word back. The Falcons remain
halted. Failure or generation fencing disables bus mastering, resets the
volatile upload and then restores command 20's page mode; an unverified reset
remains fenced and retryable. The next package must compile the complete
topology-dependent MMIO and context-switch lists before it may start FECS or
publish any channel state.

`R2.2q` freezes that complete hardware-inactive GR plan in one package. A
deterministic generator consumes the MIT-licensed Nouveau files at the same
pinned Linux commit and checks in all 30 ordered GK208 static MMIO packs (115
tuples) plus the exact HUB, GPC0, GPC1, TPC and PPC context-switch packs (199
tuples). Pack boundaries, tuple counts and IEEE CRC32 values are immutable.
Ring 3 reads `0x409604` and one TPC-count/PPC-mask pair per reported GPC through
the read-only aperture, then rejects zero, overflowing or cross-field-
inconsistent topologies. A fixed-storage compiler reproduces Nouveau's
maximum-32 contiguous-register coalescing for all five Falcon transfer lists.
The manifest also records the future HUB start writes at `0x40910c` and
`0x409100`, readiness bit `0x80000000` at `0x409800`, context-size readback at
`0x409804` and the bounded 2000-ms reference deadline. This package executes
none of those writes or transfers. Full topology-dependent dynamic register
initialization, rollback-capable mediated execution, Falcon start, channel
publication and a real fence remain prerequisites for capability publication.

`R2.2r` turns that material into one complete, still hardware-inactive
execution image. Its fixed 64-byte header binds the validated topology, exact
used length, operation count, section counts and IEEE CRC32 to a stream of
16-byte semantic operations with capacity 2048. The stream preserves the
pinned `gf100_gr_init` order, expands every static tuple, derives Nouveau's
tile map and ZCULL values, includes the GK208 exception loops and establishes
all usable GK104 LTC/PGRAPH ZBC color/depth slots. Dynamic hardware inputs are
not sampled early: typed copy/mask operations retain the source-register
dependency until atomic execution. The two `gf100_gr_init_gpc_mmu` fault
buffers remain distinct unresolved 128-KiB, 128-KiB-aligned device-VRAM offset
operations; neither a CPU physical address nor the mediated system-memory DMA
pool is substituted. The five context groups, HUB start, readiness wait and
nonzero context-size readback are explicit final operations.

Ring 3 recompiles the stream through an independent comparison sink, rejects
every topology, order, operation or CRC mutation, and stages only the used
prefix at pool offset `0x72000`. Every byte is read back before the existing
relocation seal. No operation in the image is executed by this package and no
new Device-Control command or authority is added.

`R2.2s` adds append-only Device-Control command 22 as the hardware-inactive
kernel acceptance boundary for that sealed image. The immutable physical
profile derives the actual VRAM aperture containing the validated VBE scanout
from PCI BAR geometry; it does not assume a BAR number. Ring 0 independently
checks the 64-byte header, exact used prefix, operation CRC, a twice-sampled
live topology CRC, all semantic opcode/address bounds and exactly the two
distinct unresolved 128-KiB fault-buffer records. Fixed upstream GK104/GK208
FB and LTC registers determine bounded partition, VRAM and tag-RAM geometry.
The mediator records two aligned fault buffers and one tag region after visible
scanout, below both the aperture and the probed VRAM limit, without returning
an offset or BAR base to Ring 3. This is a logical generation reservation only:
command 22 writes no VRAM or MMIO and starts no Falcon. For the exact profile,
commands 20 and 21 reject attempts that bypass this prerequisite state. Fence
and generation cleanup erase it idempotently.

`R2.2t` adds append-only Device-Control command 23 as that single hardware-active
transaction. After commands 22, 20 and 21, Ring 0 re-samples topology and
revalidates the sealed image and opaque VRAM/LTC plan before the first write.
It zeroes only the two clipped 128-KiB fault-buffer windows, programs the pinned
GK104 LTC count, tag-base and page-mode registers, clears the bounded CBC tag
range, and then interprets every typed GR operation in order. Context groups
consume exactly their immediately following transfers. CBC, idle and FECS
readiness waits sleep or yield and share one monotonic five-second transaction
deadline. Success exposes only operation count, a nonzero context size and a
ready flag. Any partial failure performs a GR reset before retry or fencing,
and generation cleanup clears all readiness and reservation state only after
that reset. Channel/runlist binding, USERD kick, bus mastering, IRQs, command
submission, fences and NVIDIA acceleration capabilities remain disabled for
the next hardware gate.

QEMU and VMware cannot emulate GK208.  Automated gates therefore cover source
contracts, driver lifecycle, both channel and GPU-VM layouts and non-regression of the
VMware accelerated path.  The `NVIDIA_GK208_PROBE` and
`NVIDIA_GK208_READY` markers require one final manual boot on the ASUS target.

The first ASUS run also established that the optional driver may be absent
while loader framebuffer metadata is still published.  Published metadata is
not evidence that the corresponding graphics mode remains visible after the
rescue shell used VGA text.  Runtime activation is therefore idempotent only
when an explicit backend is active.  If the supervised endpoint returns
`ENODEV`, the desktop now re-enters the sealed VBE mode, emits
`DISPLAY_SOFTWARE_FALLBACK`, and renders in software.  Shutdown likewise uses
the direct validated VGA restoration path if the endpoint disappears.

The subsequent ASUS diagnostics exposed an independent admission defect:
the canonical identity `nvidia-gk208-ring3` needs 19 bytes including NUL, but
the common supervisor name buffer previously held only 16. The fixed capacity
is now 32 bytes, still rejects truncation, and keeps the protected descriptor
within its 64-byte critical-object payload. This permits the already bounded
driver to spawn; it does not change the passive probe, GPU authority or zero
acceleration capabilities.

The admitted driver also exposed a scanout-ownership distinction during the
second ASUS run. A supervised process restart fenced both VMware and NVIDIA by
calling the generic display deactivation path. That is required for VMware,
whose driver owns the active SVGA mode, but it incorrectly restored VGA text
for passive GK208 while the desktop still used the kernel-owned VBE scanout.
Recovery now deactivates scanout only for `svga2d-ring3`. Both drivers still
prove mediated-I/O quiescence before device fencing, process reaping and owner
recovery. Since GK208 continues to advertise zero capabilities and owns no GPU
command state, retaining sealed VBE across its generation change grants no
stale driver output authority. Explicit desktop shutdown still restores VGA.

The same reproduction showed why the generation changed during startup. The
desktop already split font and asset reads into fixed calls, but a voluntary
yield between calls could immediately select the application class again and
did not guarantee time for supervised driver heartbeats. Startup reads now
use 24-KiB chunks and sleep for one bounded millisecond between them. The
3-MiB maximum font therefore exposes 128 scheduling points while each storage
syscall and the overall initialization remain bounded; actual wake latency is
allowed to follow the system timer resolution.

## Desktop startup splash

After successful display activation and validation, the Ring-3 desktop now
publishes a deterministic dark background and the exact `REIST OS` title
before performing optional filesystem reads. It then reads the fixed
`/usr/share/images/reist-splash.bmp`, decodes the 512x288 uncompressed BMP3
through `libreistimage`, centers it, and adds the loading label with the trusted
framebuffer font. The splash remains the visible scanout while the Unicode
font, icon cache, trash state and file associations initialize; the first full
desktop frame replaces it.

The artwork is presentation data, not boot authority. Reads are chunked and
bounded to 512 KiB, dimensions and format are checked after decoding, and all
decoder memory is caller-owned. During this phase the decoder shares the
later font-mapping workspace, while encoded and decoded data occupy disjoint
regions of the existing aligned font-file buffer. There is no startup heap or
kernel image parser. A missing, malformed, oversized or display-incompatible
asset leaves the already visible title fallback and does not block desktop
startup. The rescue floppy intentionally omits the artwork.
