# R3.33: large Surface resize and bounded browser raster

Frozen on bd421a50, 2026-09-08. Direct main-worktree execution; one mediated
Surface geometry/publication/resource-lifecycle boundary, no later package.

## Failure and scope

A high-resolution desktop can grow decorations beyond the old1024x768
Surface limit. desktop_surface_reconfigure rejects it and leaves the old
client size. The kernel buffer mediator, CSS request/scene/layout/raster and
8-MiB aggregate buffer store independently impose the same obsolete ceiling.
The reference is the existing wl_surface/xdg_toplevel-inspired configure,
acknowledge and commit adapter: geometry is authoritative after matching ACK;
pixel publication remains transactional and generation-scoped. No Wayland
wire compatibility claim. No new complex kernel driver or GUI policy in Ring0.

## Frozen requirements

- Geometry profile2 admits positive dimensions <=4096 per axis and at most
  4,194,304 XRGB8888 pixels (16MiB), matching current display scanout admission.
  Preserve wire layouts, operation numbers, protocol versions, byte/pixel
  units, stride validation, rights, generation and old valid dimensions.
  All components must use the same dimension AND area envelope; overflow,
  zero, excessive stride/area are rejected before mutation. Installed images
  rebuild matching clients; no mixed old-client high-resolution guarantee.
- Existing kernel-owned immutable buffer mediator keeps8 slots, construction,
  revocation, reference and bitmap cleanup. Aggregate storage becomes a
  bounded64MiB cache allocated once on first use through existing kernel heap,
  not added to loadable BSS (native ELF must remain below64MiB). Admission
  checks free physical memory and retains the established1/16 recovery reserve.
  Preemption is disabled only across initial allocation/ownership publication;
  no allocation in IRQ/fatal/reap/draw. Allocation failure publishes nothing,
  returns bounded error and permits retry. Global cache retains backing;
  retiring owners frees generation-owned blocks, not the global cache.
  No allocator, scheduler, boot-loader or driver command changes.
- Browser private workspace remains <=36MiB. Replace maximum-size embedded
  pixel array by an independently allocated, capacity-checked private raster
  buffer <=16MiB. Grow only as needed, preserve old allocation on OOM and
  release at shutdown. Every preflight/publication raster site reserves first.
  Normal typing does not allocate, reflow or republish page pixels.
- CSS request, scene, controls, layout and raster use the same bounded viewport
  geometry. Preserve font/image/DOM/CSS/JS quotas and cached script replay.
  Raster overdraw admission becomes proportional to actual viewport area,
  max four pixel passes with the existing4Mi-operation floor and a hard16Mi
  ceiling. Reject before writes. No unbounded per-page work or larger scripts.
- Real native O0/O2 regression first: Surface1024->1600->2560 and back with
  pending ACK/old authority, malformed/overflow/area/buffer cases; actual
  kernel mediator allocation failure, exhaustion, owner/consumer fencing,
  referenced destruction, reuse and rollback; actual browser raster allocation,
  publication damage, OOM cleanup; actual CSS/TTF high-resolution layout.
- Both images, kernel link/memory host and actual memory-resilience guest.
  New headless browser guest at1600x900 and2560x1440: real max/restore caption
  and resize input, acknowledged Surface AND accepted browser scene, scanout
  of right/bottom chrome/page, repeated wheel redraw and retained state,
  isolated browser fault/replacement and fresh shell. Existing low-resolution
  resize/layout/fault/hang and browser input gates unchanged. Probe code
  observes only; no synthesized success or viewport mutation.
- Protect BENCHMARK, MATHTEST, TEXTTEST, CURL, JSTEST, JSWORK payload bytes;
  kernel delta restricted to buffer mediator admission/storage. No new
  VMware-performance claim; keep previous failure evidence and all gates.
  Failed gates retry only after demonstrated focused correction. No visible
  VM/Windows error dialogs, nested agents, destructive cleanup or push.
