# REIST GUI source layout

This tree separates graphical session components and GUI clients from console
programs in `userspace/programs` and interactive console programs in
`userspace/bin`.

## Source hierarchy

| Path | Responsibility |
|---|---|
| `compositor/` | Trusted session compositor and window manager. It alone owns global placement, Z-order, focus, input routing, composition and display publication. |
| `apps/<name>/` | GUI applications, one directory per program. Until Surface IPC exists, explicitly documented full-screen clients may use only the public display ABI and GUI library. |
| `examples/` | Small, buildable SDK examples that include only installed public headers. |
| `include/reist/gui/` | Versioned public C APIs. In-process library APIs and the future cross-process Surface protocol remain explicitly separate. |
| `lib/` | Bounded, reusable Ring-3 GUI components. Menu and dialog state machines are renderer-independent static-library components. |
| `share/` | Future versioned themes, fonts, icons and other read-only GUI resources. |

Directories are created when their first real source or resource is added.
This avoids placeholder modules and keeps the current trust boundary visible.

## Installed hierarchy

- `/usr/gui/bin/desktop.prg` contains the trusted session compositor.
- `/usr/gui/bin/guidemo.prg` is the interactive control and dialog gallery.
- `/usr/gui/bin/*.prg` contains directly launchable GUI applications.
- The development sysroot installs public headers under `/usr/include` and
  static archives under `/usr/lib`, following conventional compiler lookup
  rules. No runtime dynamic-library location is claimed before a loader ABI
  exists.
- `/usr/gui/share/` remains reserved for architecture-independent desktop
  resources until its target-image contract is fixed.

The Ring-3 shell includes `/usr/gui/bin` in its bounded default search path, so
`desktop` and `guidemo` are directly launchable. `/DESKTOP.PRG` and the previous
`/usr/bin/desktop.prg` path remain compatibility aliases, but new code must use
the installed canonical path.

`desktop.prg` is currently a single compositor process. Moving its sources and
using `libreistgui.a` does not claim that GUI clients are isolated already;
that boundary begins with the versioned Surface and event protocol described
in the desktop workflow.

## Public API and build contract

The installed component APIs currently comprise `<reist/gui/types.h>`,
`<reist/gui/menu.h>` and `<reist/gui/dialog.h>`. They supply fixed-capacity,
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
responses, buttons, keyboard focus, Enter/Escape handling, close action and
title dragging with pointer capture. Components that do not yet have a public
implementation are listed as disabled plans and are not drawn as misleading
mock controls. Until the versioned Surface IPC exists, the gallery is a
full-screen display client; it is not presented as an isolated compositor
surface.

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
```

The resulting sysroot contains:

```text
build/sdk/usr/include/x86os.h
build/sdk/usr/include/reist/gui/types.h
build/sdk/usr/include/reist/gui/menu.h
build/sdk/usr/include/reist/gui/dialog.h
build/sdk/usr/lib/crt0.o
build/sdk/usr/lib/libreistos.a
build/sdk/usr/lib/libreistnetparse.a
build/sdk/usr/lib/libreistgui.a
build/sdk/usr/lib/pkgconfig/reist-gui.pc
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
