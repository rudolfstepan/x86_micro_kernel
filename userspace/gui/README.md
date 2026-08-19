# REIST GUI source layout

This tree separates graphical session components and GUI clients from console
programs in `userspace/programs` and interactive console programs in
`userspace/bin`.

## Source hierarchy

| Path | Responsibility |
|---|---|
| `compositor/` | Trusted session compositor and window manager. It alone owns global placement, Z-order, focus, input routing, composition and display publication. |
| `apps/<name>/` | Future GUI client processes. One directory per application; clients have no direct framebuffer or global-window authority. |
| `include/reist/gui/` | Future versioned public client ABI. Only types and operations shared across process boundaries belong here. |
| `lib/` | Future bounded Ring-3 GUI client library and controls built on the public ABI. |
| `share/` | Future versioned themes, fonts, icons and other read-only GUI resources. |

Directories are created when their first real source or resource is added.
This avoids placeholder modules and keeps the current trust boundary visible.

## Installed hierarchy

- `/usr/gui/bin/desktop.prg` contains the trusted session compositor.
- `/usr/gui/bin/*.prg` will contain directly launchable GUI applications.
- `/usr/gui/lib/` and `/usr/gui/share/` are reserved for the corresponding
  runtime libraries and architecture-independent resources.

The Ring-3 shell includes `/usr/gui/bin` in its bounded default search path, so
`desktop` remains directly launchable. `/DESKTOP.PRG` and the previous
`/usr/bin/desktop.prg` path remain compatibility aliases, but new code must use
the installed canonical path.

`desktop.prg` is currently a single compositor process. Moving its sources does
not claim that GUI clients are isolated already; that boundary begins with the
versioned surface and event ABI described in the desktop workflow.
