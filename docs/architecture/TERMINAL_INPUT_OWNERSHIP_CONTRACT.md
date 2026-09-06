# Terminal input ownership (R3.12)

Reference: POSIX terminal foreground process groups and `tcsetpgrp`,
<https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap11.html>
and <https://pubs.opengroup.org/onlinepubs/9699919799/functions/tcsetpgrp.html>.
REIST implements an explicitly different, versioned single-terminal adapter,
not POSIX job control, sessions, termios, signals or source compatibility.

## Authority and lifecycle

One fixed eight-entry foreground chain stores exact process identities. The
root userspace shell attaches the console; it transfers input to a validated
live child before waiting or detaching. A supervised compositor may acquire
foreground input from the console after its userspace initialization. The
manually launched compositor receives the same authority through shell
transfer, not through its filename or a test-only bypass. Window focus and
Surface keyboard delivery remain compositor policy in Ring 3.

Foreground programs that launch an interactive child must explicitly use
the same transfer operation. GTEST's Unicode raster probe delegates to its
unreaped desktop child's PID/generation and checks foreground return after
successful wait. Failed admission cancels/reaps that child and reports test
failure. No special desktop acquisition, implicit spawn inheritance or
test-only kernel privilege is introduced. Its existing test wait and the
unchanged 180-second host gate remain the probe's execution envelope; this
does not introduce a new timed-wait API.

Only the current foreground generation can consume terminal characters.
Transfer validates the complete request, live identities and parent generation
before touching the chain or keyboard queue. An unrelated process cannot
claim input. Release restores the preceding owner. Death truncates the chain
at the exact dying generation; repeated cleanup and stale cleanup are inert.
Every actual transition flushes queued bytes before waking readers. No keys
queued for an old owner are delivered to its successor. Physical keys pressed
after a transition are new input; this is not a hardware timestamp protocol.

Process-table -> terminal-input is the validation lock order. Admission and
dequeue share the terminal-input spinlock. No process-table lookup, allocation,
VFS operation or display-driver call occurs under the dequeue lock. Ownership
is fenced when termination is admitted, before potentially sleeping teardown.

## Compatibility adapters and bounds

`getchar`, `WAIT_ENTER`, nonblocking getchar and terminal descriptor `read`
(including descriptor aliases) all pass the same pre-dequeue admission.
Blocking getchar retains its blocking behavior and existing single wait node,
with bounded 10-ms device fallback waits. Excluded nonblocking readers receive
zero; excluded terminal `read` receives `-EAGAIN`. These are deliberate legacy
adapters, not POSIX `SIGTTIN`/`EIO` semantics. No keystroke value is exposed by
an ownership query. The kernel rescue reader is admitted only with no Ring-3
foreground owner. Regular foreground shell programs are explicitly handed
input; browser workers never inherit it implicitly.

The chain is fixed capacity, operations traverse at most eight identities,
process lookup traverses at most MAX_PROGRAMS slots. ABI request sizes and
reserved fields are exact. Full chains fail without flushing or publication.
No mouse routing or device-driver architecture changes belong to this package.

The v1 24-byte request is read-only and has version, exact size, operation,
zero reserved word and an optional exact target identity. Unknown operations,
versions, sizes and nonzero reserved/unused fields return `-EINVAL` (22);
bad user memory returns `-EFAULT` (14). Forbidden admission returns `-EACCES`
(13), conflicting foreground acquisition `-EBUSY` (16), full chain `-ENOSPC`
(28), and a missing/dead target generation `-ESTALE` (116). An exiting caller
returns `-ESRCH` (3). CHECK returns zero only to the foreground generation,
otherwise `-EAGAIN` (11). Failed calls never flush keys or change ownership.

The independent display-driver supervisor retains scanout across compositor
failure. This mediator does not deactivate hardware in a process-table lock.
Normal Ring-3 desktop exit first deactivates display, then releases input;
unhandled death restores console input before slower teardown. The diagnostic
guest proves console command dispatch after death and visible console return
after a freshly launched desktop's normal exit; it does not claim an automatic
supervised-compositor crash/restart proof beyond the existing frozen gates.

## Acceptance

2026-09-06: all seven frozen host groups (149 PASS, two existing skips), both
reference builds and all five QEMU runtime gates passed. The corrected
harness keeps its fixed overall deadline across two real keyboard sessions,
compositor UD2, fresh generation and console return. Normal browser, external
resources, scrolling, supervised desktop relaunch and memory resilience pass.
The user-authorized GTEST caller repair adds eight real caller/mediator host
cases; the final memory gate proves Unicode raster, complete GTEST and shell
return with admission intact. Exact commands, durations, hashes, limitations
and preserved earlier failures are in CURRENT_WORK.md.
