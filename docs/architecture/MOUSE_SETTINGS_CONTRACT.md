# Mouse settings (R3.31)

Frozen on accepted `e5255931`, 2026-09-08. One Ring-3 settings/publication and
session-input boundary: dedicated Control Panel applet, atomic saved profile,
startup application before input, no live-policy switch or kernel change.
Execute directly in the visible worktree, no agents or push.

## Reference and deliberate adapter

Use established primary/secondary buttons, natural scrolling, milliseconds
and linear/adaptive pointer profiles as in
[libinput configuration](https://wayland.freedesktop.org/libinput/doc/latest/api/group__config.html).
The existing `reist.input/1` percent scale is a REIST adapter, not libinput API
or device-independent/DPI-normalized acceleration compatibility. Existing
relative mouse-event v1 has no absolute-position flag or device timestamps;
do not invent such metadata or extend driver authority in this package.

Keep the five existing keys/ranges: primary_button left/right, speed_percent
25..200, acceleration flat/adaptive/off, natural_scroll false/true,
double_click_ms 200..1000. `flat` is constant percent gain. `off` is raw1:1
(the UI labels this explicitly). Adaptive uses bounded session-observed
velocity and at most twice the selected gain, with no extra clock calls in
flat/off. First report, generation change or unavailable/nonmonotone time
uses base gain and resets velocity history; cap observation intervals.
No claim of physical VMware absolute-pointer calibration.

Ship flat100/left/normal/500 to preserve the previously unscaled effective
defaults (the old adaptive key was inert). Missing keys use these defaults;
any malformed known value rejects the complete candidate, never a partial
profile. Preserve unrelated keyboard and future entries when saving. A
versioned, fixed-size userspace settings value and fixed fractional-motion
state use checked64-bit arithmetic, saturate outputs and safely invert
INT32_MIN wheel input. Generation changes reset fractional state. Normalize
button masks once before every consumer; middle/other bits survive. Apply
wheel direction once before existing Explorer/Surface v120 conversion.
Use the selected double-click interval in desktop icons, shortcuts, control
panel, trash and Explorer windows; capture and drag/resize rules unchanged.

## Authority, persistence and UI

MOUSE.PRG is a separate ordinary Surface client, with native range/button
controllers, keyboard Tab/arrows/Enter/Escape, mouse capture, visible values,
Save/Defaults/Close and a local double-click test area. Small-window and
failed-config paths stay usable and fail closed; never claim save on spawn
alone. Explicitly show that settings become active at next desktop start.

Append OPEN_MOUSE after OPEN_DISPLAY in the existing v6 Surface envelope;
keep all previous numbers, sizes and wrappers. Only the exact compositor-bound
Control Panel generation may request this fixed applet. Validate all reserved
bytes and live owned Surface before queueing; coalesce, recheck generation
on consumption, clear on retire. No arbitrary launch path or live-input
authority crosses the request. Preserve existing Display broker behavior.

Extend existing CONFIG `set TARGET KEY VALUE` append-only to at most five
distinct key/value pairs in one invocation (13 argv entries, below existing
16-entry ABI). Validate every pair before file access, read once, set all,
then reuse the existing temp/fsync/close/rename transaction exactly once.
Failure before publication preserves the original file. No new persistent
format or second applet writer. Cross-process simultaneous edits retain the
existing last-publisher policy; no new multi-writer transaction claim.

Applet owns at most one CONFIG child, one immutable pending snapshot, a5s
deadline and generation-checked cancellation/reap. Only observe-exited then
wait. On error/readback mismatch preserve editing state and report failure.
Close during save cancels boundedly, never waits on a live child; applet fault
and replacement must leave Desktop/Control Panel/terminal usable. Only an
explicit diagnostic launch allows a real UD2 fault gesture; no normal crash
shortcut or host error dialog. Startup config I/O occurs once, not on motion,
wheel, paint or reconnect; invalid/unavailable config falls back with one
bounded diagnostic. No new queue/busy-wait/heap or per-motion redraw.

## Frozen acceptance

Exactly one cohesive package. Scope is the queue's allowed_files. Native O0/O2
settings/motion/overflow/generation, actual CONFIG transaction failures and
preservation, async applet lifecycle/readback/cancel, real range/control input
and broker authority/replay tests; existing GUI/Explorer/Surface/config/shell
regressions. Both reference builds and independent actual FAT/kernel hashes
protect BROWSER/HTMLWORK, benchmark and six other established programs.
Real headless QEMU uses shell `mouse --list`, Control Panel mouse/keyboard
launch, real slider/button input and pixels, Save/Close/reopen, applet fault
and replacement, desktop restart, observed scaled pointer/buttons/wheel,
default restoration and shell return. Separate unchanged browser-input and
inner-corner resize/wheel/fault/recovery guests protect the previous result.
No complete Workstation/hardware acceptance claim. Keep all failures and
repeat only affected gates after a demonstrated focused correction.
