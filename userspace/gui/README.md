# REIST GUI source layout

This tree separates graphical session components and GUI clients from console
programs in `userspace/programs` and interactive console programs in
`userspace/bin`.

## Source hierarchy

| Path | Responsibility |
|---|---|
| `compositor/` | Trusted session compositor, window manager and bounded Explorer adapter. It alone owns global placement, Z-order, focus, input routing, composition and display publication. |
| `apps/<name>/` | GUI applications, one directory per program. Migrated clients use only local Surface paint/input APIs; explicitly documented legacy clients may still use the public full-screen display ABI. |
| `examples/` | Small, buildable SDK examples that include only installed public headers. |
| `include/reist/gui/` | Versioned public C APIs. In-process component APIs and the cross-process Surface protocol remain explicitly separate. |
| `lib/` | Bounded, reusable Ring-3 GUI components. Menu, dialog, container, tab, value-control and multiline text-editor state machines are renderer-independent static-library components. |
| `share/` | Future versioned themes, fonts, icons and other read-only GUI resources. |

Directories are created when their first real source or resource is added.
This avoids placeholder modules and keeps the current trust boundary visible.

## Installed hierarchy

- `/usr/gui/bin/desktop.prg` contains the trusted session compositor.
- `/usr/gui/bin/guidemo.prg` is the interactive control and dialog gallery.
- `/usr/gui/bin/notepad.prg` is the bounded graphical text editor.
- `/usr/gui/bin/soundplayer.prg` is the bounded graphical WAV player.
- `/usr/gui/bin/imageviewer.prg` is the bounded BMP/GIF image viewer.
- `/usr/gui/bin/*.prg` contains directly launchable GUI applications.
- The development sysroot installs public headers under `/usr/include` and
  static archives under `/usr/lib`, following conventional compiler lookup
  rules. No runtime dynamic-library location is claimed before a loader ABI
  exists.
- `/usr/gui/share/` remains reserved for architecture-independent desktop
  resources until its target-image contract is fixed.

The Ring-3 shell includes `/usr/gui/bin` in its bounded default search path, so
`desktop`, `guidemo`, `notepad` and `soundplayer` are directly launchable. `/DESKTOP.PRG` and the previous
`/usr/bin/desktop.prg` path remain compatibility aliases, but new code must use
the installed canonical path.

`desktop.prg` is the session compositor. Migrated applications such as
`notepad.prg` remain separate Ring-3 processes and communicate through the
versioned Surface/event protocol; legacy full-screen clients are called out
explicitly until they are ported.

The Surface lifecycle is strict: the desktop delegates a generation-scoped
endpoint, the client acknowledges configure and submits a bounded pending
paint list. Only `paint_commit` replaces the retained visible list. Fill and
text geometry is client-local and clipped by the compositor. Resize uses a new
configure serial and acknowledgement. Process exit or a broken IPC channel
revokes endpoint and surfaces; a recycled PID alone never restores authority.
The broker drains the four-message IPC queue in a fixed 64-round cooperative
round-robin budget, so large retained frames cannot starve another client or
turn ordinary menu hover into a send timeout.

## Public API and build contract

The installed component APIs currently comprise `<reist/gui/types.h>` and the
initial cross-process contract `<reist/gui/surface.h>`,
`<reist/gui/menu.h>`, `<reist/gui/dialog.h>`,
`<reist/gui/control.h>`, `<reist/gui/container.h>`,
`<reist/gui/tabs.h>`, `<reist/gui/value_controls.h>`,
`<reist/gui/text_editor.h>` and `<reist/gui/file_dialog.h>`. They supply fixed-capacity,
heap-free state machines,
local geometry queries, implicit pointer capture, keyboard navigation and
bounded damage output. The dialog API additionally models owner identity,
modal/modeless routing, semantic button roles, completion responses and title
dragging without a nested event loop. They deliberately supply no renderer and
have no dependency on `x86os.h`, the framebuffer or compositor internals. The
complete ownership, field and return-value contracts are written as
Doxygen-compatible comments in the public headers. The component inventory and
roadmap live in `docs/architecture/GUI_CONTROLS_AND_DIALOGS.md`.

## Interactive control gallery

`userspace/gui/apps/control_gallery/main.c` builds as `GUIDEMO.PRG` and is
installed as `/usr/gui/bin/guidemo.prg`. Start it from the Ring-3 shell with:

```text
C:\>guidemo
```

The gallery exercises every currently public interactive component: menu bar,
popup menu, modeless dialog, application-modal dialog, semantic dialog
responses, nested page/group containers, tabs, label, general pushbutton,
checkbox, exclusive radio group, text field, list selection, scrollbar,
slider, spin box, progress indicator, keyboard focus, Enter/Escape handling,
close action and title dragging with pointer capture. Components without a
public implementation are not drawn as misleading mock controls. The
versioned Surface IPC exists, but the gallery has not yet been migrated and
therefore remains an explicitly documented full-screen compatibility client.

## Graphical text editor

`userspace/gui/apps/notepad/main.c` builds as `NOTEPAD.PRG` and is installed
as `/usr/gui/bin/notepad.prg`. It can be started directly or through the
desktop's validated `/etc/reist/filetypes.conf` associations:

```text
C:\>notepad /readme.txt
```

The application uses the public multiline controller, menu, asynchronous
dialog and file-dialog APIs. The File menu provides Open, Save and Save As;
the application validates the selected absolute path and retains ownership of
all VFS operations. It loads at most the documented fixed document capacity, shows a
dirty marker, supports pointer cursor placement and keyboard navigation, and
saves through a process-unique temporary file, `fsync` and same-directory
rename. Save/discard/cancel on exit is application-modal. From the desktop it
runs asynchronously in a compositor-decorated, movable and resizable Surface
window. Direct shell invocation retains a compatibility full-screen path.

## Graphical sound player

`userspace/gui/apps/sound_player/main.c` builds as `SOUNDPLAYER.PRG` and is
installed as `/usr/gui/bin/soundplayer.prg`. The validated
`/etc/reist/filetypes.conf` association starts it when a `.wav` icon is opened
in Explorer. It uses only the public `libreistgui` and `libreistaudio` APIs and
offers keyboard- and mouse-operable Abspielen, Stop and Schliessen controls.
Audio cleanup is idempotent on every exit path. The current cyclic audio ABI
loads at most 15360 frames; streaming and progress seeking require a later
versioned queue ABI. Unlike the migrated Notepad and Image Viewer, the player
still uses the supervised full-screen compatibility path; its Surface-client
migration remains open.

## Isolated window clients

`/usr/gui/bin/surfacedemo.prg` is the first program that remains a separate
Ring-3 process while the desktop continues composing. The desktop reserves an
IPC endpoint before spawning it, delegates only that endpoint to the exact
process generation, and owns placement, focus, movement, resizing, decoration
and close delivery. `notepad.prg` uses the same public contract for real
application rendering and local input. Applications receive no direct
framebuffer authority.

## Graphical image viewer

`userspace/gui/apps/image_viewer/main.c` builds as `IMAGEVIEWER.PRG` and is
installed as `/usr/gui/bin/imageviewer.prg`. Desktop associations for `.bmp`
and `.gif` launch it without leaving the graphical session. Image parsing is
provided exclusively by the public, reentrant `libreistimage` API; the viewer
does not duplicate codec logic. Demo images are installed below
`/usr/share/images`. The viewer publishes immutable XRGB8888 buffers through
the Surface attach/damage/commit contract; only its generation-bound parent
compositor can draw clipped pixels from that resource. Resize replaces the
buffer atomically and releases the old generation afterwards.

Build the SDK and the documented example with the repository's upstream
Zig/LLVM toolchain integration:

```powershell
python scripts/build_user_sdk.py --output-dir build/sdk
python scripts/build_user_program.py userspace/gui/examples/menu_controller.c `
  --output build/programs/MENUDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/dialog_controller.c `
  --output build/programs/DIALOGDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/basic_controls.c `
  --output build/programs/CONTROLDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/nested_containers.c `
  --output build/programs/CONTAINERDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/tab_sheet.c `
  --output build/programs/TABSDEMO.PRG `
  --sysroot build/sdk -l reistgui
python scripts/build_user_program.py userspace/gui/examples/value_controls.c `
  --output build/programs/VALUESDEMO.PRG `
  --sysroot build/sdk -l reistgui
```

The resulting sysroot contains:

```text
build/sdk/usr/include/x86os.h
build/sdk/usr/include/reist/gui/types.h
build/sdk/usr/include/reist/gui/surface.h
build/sdk/usr/include/reist/gui/surface_client.h
build/sdk/usr/include/reist/gui/menu.h
build/sdk/usr/include/reist/gui/dialog.h
build/sdk/usr/include/reist/gui/control.h
build/sdk/usr/include/reist/gui/container.h
build/sdk/usr/include/reist/gui/tabs.h
build/sdk/usr/include/reist/gui/value_controls.h
build/sdk/usr/include/reist/gui/text_editor.h
build/sdk/usr/include/reist/gui/file_dialog.h
build/sdk/usr/include/reist/image.h
build/sdk/usr/lib/crt0.o
build/sdk/usr/lib/libreistos.a
build/sdk/usr/lib/libreistnetparse.a
build/sdk/usr/lib/libreistgui.a
build/sdk/usr/lib/libreistimage.a
build/sdk/usr/lib/pkgconfig/reist-gui.pc
build/sdk/usr/lib/pkgconfig/reist-image.pc
```

Compilation uses upstream `zig cc`/Clang, ELF linking uses LLD and the static
archive uses `zig ar`. The repository does not implement a compiler, assembler,
linker, archive format or C dialect. Only the checked conversion from the
fixed-address ELF32 result to the REIST-specific MYPR v1 container is custom.
`--sysroot` selects the installed headers, startup object and base archive;
ordinary additional dependencies continue to use `-I`, `-L` and `-l`.

## Inline documentation rule

Every public header must document versions, ownership, lifetime, coordinate
system, units, capacities, flags, reserved fields, side effects, errors and
thread/process assumptions. Public functions use `@param` and `@return`
contracts. Implementations document validation boundaries and non-obvious
state transitions rather than restating individual C statements. Each public
module also requires a buildable example and a host behavior test. This keeps
the SDK usable now and provides source material for the planned book on
building a graphical operating system from scratch.
