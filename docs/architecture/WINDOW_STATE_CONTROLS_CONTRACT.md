# R3.32: server-side window state controls

Frozen on clean ef7f7d2f, 2026-09-08. One compositor-local non-client
capture/visibility/geometry boundary. Direct interactive implementation only.

## Reference and scope

Use conventional normal/minimized/maximized states, remembered normal bounds,
work-area maximization and next-visible focus, as described by Win32
[ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow).
This is a private REIST WM adapter, not Win32 API/ABI compatibility. No new
public Surface version, event, syscall, persistent format or client authority.
Keep the established close button on the left; add two right-side buttons,
minimize and maximize/restore. The latter switches between single/overlapping
window glyphs. This package does not add title double-click, snapping or
drag-to-restore. Use code-native pixel glyphs and existing rendering primitives.

## Invariants

- Fixed eight slots: minimized is distinct from closed. Minimization keeps
  the application, Surface, buffers, normal/maximized geometry and task button,
  but removes the window from composition, hit tests and keyboard focus.
  Select/open restores minimized windows in their previous display state.
  Clicking the focused visible task button minimizes it; clicking another
  selects/restores it. No placeholder or occupied-slot reuse.
- Maximize stores normal bounds exactly once and fills the existing bounded
  work area above the taskbar; restore returns to those bounds (clamped only
  if work-area constraints changed). Repeated max/min/restore cannot corrupt
  saved geometry. Disable edge resize and title dragging while maximized;
  restore provides normal movement/resizing. No per-motion I/O or allocation.
- One shared half-open caption geometry for painting and hit tests; close,
  state buttons, resize, move, client precedence. Complete button rectangles,
  not just glyph ink, accept down/up. Capture belongs to the down owner;
  release outside cancels, crossing another button cannot activate it.
  Bounded depressed feedback; no client input from non-client captures.
- Invalid targets and blocked operations fail before state changes. Dialogs
  have no state buttons; a live dialog prevents new minimize/maximize actions
  on its parent. Preserve existing modal policy, no new group/minimize ABI.
  Close/retire clears state and capture even when minimized; slot reuse never
  inherits maximization, minimization, saved bounds or a stale press.
- Minimize never sends CLOSE and never causes a respawn. Pending configure
  handshakes still progress while minimized. Consume retained paint damage
  while hidden without invalidating the desktop; restoration recomposes the
  latest accepted content. Maximize/restore uses existing configure/ACK flow.
- Existing dirty/cached rendering is preserved. Right-edge live resize also
  invalidates the swept caption buttons/title band; it must not leave ghost
  controls or force full client repaint. State transitions repaint old/new
  bounds and taskbar, not idle frames. No kernel/driver/allocator changes.

## Frozen acceptance

Native actual WM O0/O2: all button pixels, outside release/occlusion/capture,
tiny/extreme geometry, eight slots, focus, normal bounds, idempotence,
maximized drag/resize suppression, modal denial, hidden close/reuse and damage
(including moved right captions). Existing desktop, Surface and runtime host
gates. Both reference images. Independent actual FAT/kernel extraction keeps
both kernels and thirteen established programs (including Mouse/Control/
Config/Display and browser/benchmark) byte-identical to ef7f7d2f.

Headless QEMU native mouse input and scanout: real applet, caption cancel,
maximize/normal pixels and configure/ACK, minimized task button, no close,
same process and edited state after restore, maximized minimize/restore,
focused task-button toggle, maximized fault/retirement and clean replacement;
Explorer state controls, exit and fresh shell response. Diagnostics observe
state only and never mutate geometry or fabricate input/success. Existing
Mouse settings guest, inner-corner resize/layout/fault/hang/recovery guest and
browser-input guest unchanged. No visible VM/host crash dialogs, agents or
push. Keep failures; repeat only affected gates after a demonstrated scoped
repair. Exactly this package; previous VMware deferral remains untouched.
