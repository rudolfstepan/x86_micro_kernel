# Ring-3 string formatting, profile 1

Status: frozen R3.22 package, 7 September 2026; not yet accepted.

## Purpose and reference

QuickJS2026-06-04 already contains dtoa/atod and rqsort. Its actual engine
and cutils paths need snprintf/vsnprintf for errors, property names and dynamic
strings. It does not require setjmp. General numeric parsing is not a reason
to duplicate its engine-specific number conversion.

References: ISO C11 N1570 7.21.6.5/7.21.6.12 and 7.21.6.1; POSIX.1-2024
snprintf error conventions. Reuse the committed musl1.2.6 formatter and
frexpl implementation with the same verified archive as R3.21. The upstream
source/license are retained alongside generated adaptations. No complete C,
POSIX, stdio or ECMAScript compatibility claim.

## Public and private boundary

Opt-in `usr/include/reist/text/stdio.h`, `libreisttext.a`, `reisttext.pc`.
Only snprintf/vsnprintf are public; no FILE, stdout, sprintf or file functions.
Link conventional `-lreisttext -lm -lreistc` using the existing SDK. Existing
TLS formatting and all consumer defaults remain unchanged.

All standard C format conversions and length modifiers, including long double,
are part of this cohesive contract. Return the would-have-written count;
capacity zero may use NULL; nonzero capacity reserves a terminating NUL.
Truncation is not an error. Unrepresentable count is negative/EOVERFLOW,
invalid format negative/EINVAL, unrepresentable wide character negative/EILSEQ.
errno is the existing per-process storage, not a new global OS service.
The default C locale is fixed: decimal dot, no grouping, ASCII wide conversion;
narrow strings remain arbitrary bytes. There is no locale-switching API.

The memory stream is a private stack object with one caller and a bounded
destination, not a mock file service. Internal musl FILE, write and lock names
are adapted only inside the implementation. No heap, IPC, clock, VFS, console,
threads or new authority. frexpl remains private; R3.21 public libm is unchanged.
Pin-checked transformations may specialize padding to retained capacity and
correct integer-overflow edges without changing standard output semantics.
Every transformation must match the expected upstream fragment exactly.

Normal C pointer, object lifetime, format/varargs type and non-overlap
preconditions apply. A formatter is not a validator for foreign pointers or
unterminated network input. `%n` is a standard caller-authorized pointer write;
the later interpreter must never use untrusted script text as a C format.
Output bounds do not bound input-string length: work is bounded by valid input
objects, format length, retained output and fixed floating representation.
Discarded padding must not iterate billions of times for a small destination.
No wall-clock/WCET claim for arbitrary calls; the future engine still needs
its own external generation-scoped watchdog.

## Validation and preservation

Actual i386 O0/O2 formatter, C/C++ headers, independent host comparisons,
integer/floating/wide/error/truncation/count/canary vectors and large fields.
TEXTTEST runs via `/usr/bin` in both image layouts, twice per headless snapshot.
Each command checks normal child, real invalid-pointer page fault/status142,
killed sleeping child/status143, exact generation/reap and fresh normal child.
Parent errno/rounding and fresh userspace shell must survive. Failure closes
owned endpoints and reaps only the captured owned generation.

No change to kernel, browser, benchmark or existing default links. Verify their
accepted binary digests in both reference profiles. Preserve all old evidence,
images and stashes; no nested agents or push. Full frozen commands and bounds
are in `automation/reist-s03b.toml`. JavaScript/DOM, time and thread adapters
remain later independent packages, not silently activated here.
