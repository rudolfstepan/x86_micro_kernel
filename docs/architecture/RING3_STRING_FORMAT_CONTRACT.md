# Ring-3 string formatting, profile 1

Status: R3.22 accepted, 7 September 2026. Final gate evidence in CURRENT_WORK.

User-authorized resumption: include the exposed HTML generator failure and
renew HTMLWORK acceptance, retaining every other frozen invariant. The dirty
candidate is attributed to this package; no unrelated changes are included.

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
Link conventional `-lreisttext -lm -lreistc` followed by the existing SDK's
explicit `usr/lib/libclang_rt.builtins-i386.a` archive (also in pkg-config).
Actual i386 compilation requires its `__udivdi3`/`__divdi3` for 64-bit division;
the strong libc byte operations must precede its weak fallback definitions.
This is the existing arithmetic runtime, not a new duplicate division helper.
Existing
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

Implemented adaptations (only generated files, upstream originals retained):
remove the unused inttypes.h include (the used types come from stdint.h),
initialize private remaining output capacity, replace padding and trailing
fraction zeros with capacity-aware repeats, reject INT_MIN dynamic width
before negation, and widen floating precision/rounding intermediates to
int64_t. The last correction has a real O0 integer-overflow regression for
INT_MAX precision with a negative decimal exponent; both valid INT_MAX-length
results and overflowing results remain tested, not clamped to a smaller limit.
Internal memory-stream and C-locale helpers are explicitly namespaced.

Observed link closure is exactly memcpy, memset, scalbn, reist_libc_errno,
__udivdi3 and __divdi3. A real conventional guest link plus a byte-identical
symbol-bearing diagnostic verifies from the link map that byte/errno symbols
come from libreistc, not the compiler archive's weak fallback. No compiler
runtime implementation change or general fallback repair is claimed.

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

No change to kernel, browser chrome, benchmark or existing default links. Verify their
accepted binary digests in both reference profiles. Preserve all old evidence,
images and stashes; no nested agents or push. Full frozen commands and bounds
are in `automation/reist-s03b.toml`. JavaScript/DOM, time and thread adapters
remain later independent packages, not silently activated here.

## Authorized HTML-table build correction

The libc header change exposed upstream `keys %entities` nondeterminism in
the existing Hubbub generator. Only generated insertion order may change;
archive pin, entity spellings/values, node representation and runtime search
remain unchanged. An exact-match adapter selects the median distinct character
at each prefix before inserting lesser/greater subtrees. Sorted keys alone
would produce unnecessarily deep lateral chains; balanced ordering bounds
search to at most seven lateral probes per byte for this pinned ASCII alphabet.
No global Perl seed or host-environment mutation, and no old binary substitution.

Tests first: distinct Perl hash seeds/perturbation produce identical generated
tables, every pinned entity matches through actual C lookup at O0/O2, and
instrumented lookup obeys the seven-probe bound. Two independent cold parser
archive and HTMLWORK builds must match byte-for-byte. Retain both products,
bind their inputs and digests in html-rebuild.json, then require both actual
reference images to contain that worker. Reaccept actual HTML5/CSS host behavior
and the existing browser keyboard/navigation/crash/restart guest workflow.
Retain the original failed artifact guard and all passed unaffected formatter
evidence. The old random HTMLWORK hash is no longer an acceptance baseline;
kernel/BROWSER/GTEST/BENCHMARK and TEXTTEST/MATHTEST remain byte-exact.

Measured generator evidence: all 2137 pinned entity entries pass actual C
lookup at O0/O2, maximum six lateral probes per character. Two final cold
builds (106.023s together) produce identical parser archives and HTMLWORK:
845868 bytes, SHA256 c40c114e593a1251ce803ac46e0a8639b87ff061dacfe52958a5faa1f3996da8.
The artifact reader uses the native FAT short alias `benchm~1.prg` for
BENCHMARK and checks payload digests in both complete reference images.
This is content binding, not a new filesystem implementation or acceptance.
