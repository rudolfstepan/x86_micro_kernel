# Isolated JavaScript core, profile 1 (R3.23)

Frozen before implementation, 7 September 2026; accepted after 40 host cases,
both references, actual image preservation and JS/math/browser guest gates.
Exact evidence, elapsed times and retained development failures: CURRENT_WORK.md.

## Reference and boundary

Use the existing inventoried [QuickJS 2026-06-04](https://bellard.org/quickjs/)
archive, SHA256 b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a.
Reference terminology is ECMA-262 and the upstream
[embedding API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API).
Retain MIT license, original selected sources and explicitly generated
adaptations. This is an opt-in Ring-3 engine, not full ECMAScript/Test262 or
browser compatibility, not the upstream qjs CLI and not quickjs-libc.

One owner, one execution context per process. No kernel, scheduler, driver,
syscall, process quota or existing consumer/default-link change. No DOM,
network, file, module loader, bytecode import, worker threads or shared-memory
authority. Date is absent until a real wall-clock/timezone contract exists;
no fake time.h, gettimeofday or POSIX stubs. Math.random takes an explicit
nonzero embedding seed; it is not a cryptographic randomness facility.

The cohesive slice includes parser, interpreter, Unicode/RegExp, BigInt,
JSON, proxy, collections, typed arrays, promises and cycle GC. Do not split
compatible language cases into separate packages. The production adapter uses
JS_NewRuntime2 and JS_NewContextRaw with explicit intrinsic installation.
Disable only CONFIG_ATOMICS, keep actual stack checks and interrupt hooks.
Remove unselected default allocator/context, Date and stdio dump entry points
from generated sources, never replace them with empty success functions.
Any transform must match the pinned input exactly and reject drift.

The private alloca spelling maps to the compiler's actual stack allocation
builtin; upstream stack admission remains enabled. Each evaluation/collection
refreshes the upstream stack top at the embedding boundary. Both O0 and O2
compile with explicit `-fno-sanitize=all`, matching the freestanding runtime,
not Zig's implicit Windows Debug UBSan runtime. The latter enlarged the i386
interpreter frame to 29824 bytes before its stack check; that development
failure is retained. No optimization or stack-budget bypass is used in the O0
language test. This is not a sanitizer acceptance claim.

Omissions also cover the Date branch in the private object dumper and unused
RegExp/Unicode/dtoa stdio diagnostics. No public FILE facade is introduced;
the formatter is the accepted bounded memory-only implementation. The pinned
engine retains its own dtoa/atod and rqsort. The actual upstream i386 lrint
implementation uses x87 fistpl, preserving rounding and FE_INVALID without
an out-of-range C floating-to-integer cast.

Reuse the accepted process-backed libc allocator, libm and string formatter.
Needed standard additions are the ISO C integer formatting macros and musl's
real lrint conversion, with i386 C/C++ and behavioral tests. Existing 44
binary64 functions and all measured consumer binaries must remain unchanged.

## Ownership and bounds

An opaque engine owns one runtime/context. Configuration is versioned and
validated before allocation: memory budget 1..128 MiB, stack budget 4..16 KiB
(the actual guest stack is 32 KiB), source <=1 MiB and result <=64 KiB.
Allocator metadata accounts exact requested bytes and overhead, enforces the
budget before side effects, preserves old allocation on failed realloc and
returns NULL on exhaustion. Backing comes from the existing explicit process
provider, never a giant static arena. No eager allocation of the full budget.

Evaluation receives a length-delimited source, copies its exact admitted
bytes plus NUL for upstream's API, rejects embedded NUL, and has a monotonic
deadline plus a bounded pending-job count (<=1024). Conversion of results is
inside the same deadline. Do not report partial/truncated results as success.
Ordinary script/syntax exceptions are distinct from allocation, capacity and
deadline failure. Resource/deadline failure poisons the context; later calls
are rejected until destruction and fresh creation. Interrupted scripts cannot
recover authority by catching an exception or running another pending job.

QuickJS refcounts and cycle collection reclaim ordinary JS ownership.
Destroy must free all remaining runtime allocations; a corrupt/hung process
is never repaired through destructors. Cooperative interrupts do not bound all
native algorithms/GC. The external process owner therefore owns a monotonic
watchdog, exact PID/generation and bounded kill/reap/recreate/self-test path.
This package proves that boundary with JSTEST, not a new supervisor service.
Browser DOM/IPC integration must retain it in a later package.

## Frozen verification

Host regression first where practical. Real i386 O0/O2 engine and facade,
not source patterns alone: arithmetic/closures/classes, strings/Unicode,
JSON, regexp, bigint, collections, typed arrays (including half-even clamp),
promises, syntax/script exceptions, cycles/repeated create/free, long source,
large result, budget exhaustion, failed realloc and interrupt/job limits.
Validate fresh-instance recovery after failures and exact allocation cleanup.
Public facade and inttypes compile as C/C++; archive pins, extraction bounds,
adapter drift, incremental rebuild and undefined-symbol/authority allowlists.
lrint covers positive/negative ties, four rounds, inexact and invalid inputs.
Compiler commands <=90s, four workers maximum; host executions <=30s.

Both reference images package JSTEST.PRG under /usr/bin; normal Ring-3 shell
dispatch is mandatory in Windows and Make layouts. QEMU snapshot1024MiB,
overall <=180s: twice run jstest, including normal script execution, actual
fault142, externally killed noncooperative worker143, exact generation/reap,
fresh self-tested worker and surviving parent/shell. No visible host windows
or crash dialogs. Retain failed logs and images; rerun only affected gates
after a demonstrated focused in-scope correction.

Verify both actual image kernels and BROWSER/HTMLWORK/GTEST/BENCHMARK plus
MATHTEST/TEXTTEST against 6bc51cbf. Reuse their prior performance evidence,
without claiming new timing/WCET or rerunning unchanged VMware benchmarks.
Run the original math/text/libc/build/benchmark hosts, both references, the
new JS guest, existing math guest and browser input/crash/restart guest.
Final commands/allowed files are frozen in automation/reist-s03b.toml.
