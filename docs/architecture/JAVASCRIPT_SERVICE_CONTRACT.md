# JavaScript service lifecycle, profile 1 (R3.24)

Frozen 7 September 2026 on accepted a09d8841, before implementation.

## Boundary and standard reference

ECMA-262 execution semantics remain those of the accepted QuickJS core.
Existing REIST versioned IPC, process identity and private-memory contracts
provide transport and authority. This is a private little-endian i386 binary
embedding protocol, explicitly not JSON-RPC, WebDriver, HTML/DOM or a public
OS ABI. Fixed fields and generation checks are necessary for bounded parsing
and fencing. No pointer crosses a process boundary. No new system calls,
network/file/GUI authority, Date, DOM or browser scripting activation.

JSWORK owns a persistent runtime, separate from BROWSER and HTMLWORK. Its
parent owns a noncopyable C++ session with explicit asynchronous shutdown and
reap; destructors must not pretend that fallible/blocking recovery succeeded.
The existing C engine and public SDK are not changed. This slice closes the
process/transport failure model as a whole. The subsequent document/DOM/event
transaction and navigation publication protocol is a distinct boundary.

## Wire and admission

Each <=2048-byte bulk message repeats a 72-byte header: magic/version/header
size/operation, parent PID/generation, child PID/generation, document epoch,
request sequence, status, total/offset/length, two reserved zero words, and an
absolute monotonic millisecond deadline. Reserved fields and malformed,
truncated, duplicate, overlapping, changed-total or out-of-order frames fail
before copy or accepted state. Both peers compare all session identity fields.
One command in flight; two endpoints delegate only RECEIVE on requests and
SEND on replies to the worker. Untrusted page code cannot select endpoints.

HELLO carries the owner's nonzero 64-bit random seed. Worker initializes its
explicit 64-MiB process heap and accepted 32-MiB engine/16-KiB stack, executes
a real self-test, and only then acknowledges with validated profile limits.
EVAL admits <=1 MiB exact source and <=65535 UTF-8 result bytes plus NUL.
Partial reply bytes remain private staging, never a published result. GC and
HEALTH return versioned engine statistics; SHUTDOWN destroys the context,
checks complete heap release and exits normally. No transparent script replay.

The C++ interface borrows source and writable staging only while a command is
in flight. A completed result is a read-only view of caller-owned storage;
later cancellation or destruction invalidates the view without writing into
that storage. A targeted before/after regression covers this lifetime rule.

Every command shares one absolute deadline, at most 5000ms including transfer,
execution and reply. Parent poll performs at most eight zero-timeout IPC calls.
Queue saturation yields control rather than busy-waiting or blocking input.
Worker idle receive waits are bounded to 1000ms and revalidate its actual
parent; normal idle does not discard a living document realm. Sequence overflow,
clock error/regression and authority loss fail closed.

## Failure and recovery

Normal syntax/script exceptions retain the realm. Resource/time/capacity or
protocol failure fences result publication and both endpoints before forced
termination. Capture exact PID/generation and parent relation; never kill a
reused/unknown identity, never wait on a live child. Cleanup has 1000ms and at
most one kill, with explicit terminal diagnostics on lost identity or deadline.
No replacement before successful reap. A new HELLO/self-test must precede
reintegration. At most two explicit fresh-runtime recoveries per document;
exhaustion remains failed. A replacement does not restore old JavaScript state.

Fault/hang/malformed-reply fixtures are explicit bounded argv selectors, not
wire operations and never inferred from scripts, URLs or HTML. Real fault and
noncooperative-loop fixtures run only in the child, with a live engine. Parent
death/channel loss releases worker state; no destructor repairs corruption.
For cancellation or malformed replies, fencing can cause the cooperative
worker to exit74 before kill wins the race; both74 and143 mean a fenced/reaped
worker, not a successful reply. Only the noncooperative fixture requires143;
the actual native fault requires142. The orphan fixture abruptly exits its
owner and observes the worker disappear without unauthorized grandchild wait.
The kernel's fixed process table retains an orphan's retired exit record until
ordinary slot reuse (process_get_info includes has_exited even with parent0).
The guest first requires exit74, parent0 and no live identity, then performs
two normal, separately reaped child allocations and still requires the old
worker PID to disappear. This corrects the original fixture's assumption of
immediate metadata removal; it does not accept a live worker or an uncleared
record as success, change the deadline, or modify kernel lifecycle behavior.

## Frozen proof

Actual i386 O0/O2 protocol and C++ session tests: exact framing boundaries and
admission-before-copy, stale identities/epochs/sequence, partial/error replies,
full queues, at most eight nonblocking calls, monotonic failure, cancellation,
failed delegation, kill/reap/identity-loss/exhaustion and healthy replacement.
Real guest twice invokes /usr/bin/jsipctst.prg from the normal shell. Exercise
persistent globals/closures and promise jobs,1-MiB source,60000-byte result,
health/GC, graceful release, actual fault142, noncooperative kill143, malformed
reply and cancellation; prove fresh self-test, exact reap and surviving shell.
Both JSWORK and JSIPCTST are packaged identically by Windows and Make.

Four compiler workers maximum,90s per compiler and30s per host executable.
QEMU1024MiB snapshot,180s overall; no visible windows or host crash dialogs.
Both references and the existing browser input/navigation/crash/restart guest
must pass. Both actual kernels and BROWSER/HTMLWORK/GTEST/BENCHMARK/MATHTEST/
TEXTTEST/JSTEST remain byte-identical to a09d8841. No new timing/WCET or full
browser-JavaScript claim. Frozen commands and paths are in the queue. Preserve
all previous images and failed evidence; only affected gates are repeated
after demonstrated in-scope repairs. No agents or push.
