# OS JavaScript runner, profile 1 (JS2 / R3.35)

Frozen on ef9fb2de, 8 September 2026, before implementation.

## Boundary and references

This is the second cohesive step of OS_JAVASCRIPT_SCRIPTING_WORK_PAPER.
ECMA-262 language execution uses the pinned, accepted QuickJS engine.
The [QuickJS embedding and CLI reference](https://bellard.org/quickjs/quickjs.html)
provides `-e`, `scriptArgs` and space-separated `print`/`console.log` terminology.
`console.error` uses the same string conversion with a distinct stderr record.
This is not the complete [WHATWG Console](https://console.spec.whatwg.org/):
no format substitution, inspector or browser console implementation is claimed.
REIST-specific `reist.setExitCode(n)` selects normal completion status 0..125;
it does not terminate execution or grant process authority. No Node/qjs/std/os
compatibility, REPL, module loading, Date, file/network/spawn or admin bindings.

Move the reusable protocol/session/worker into userspace/js. Thin browser-path
adapters preserve existing consumers/tests; the canonical owner namespace is
reist::script. Browser and CLI use the same code, never the same worker/realm.
The engine stays OS-free. An opt-in, memory-only native console adapter installs
scriptArgs, print, console.log/error and reist.setExitCode only for a SCRIPT
transaction. Browser EVAL receives none of them. No evaluation in the runner.
JSWORK still restricts itself before parsing/evaluating script inputs; only
the trusted Ring-3 runner opens the explicitly requested source and emits output.

## CLI and admission

`js FILE [args...]`, `js -e SOURCE [args...]`, `js --help` and `js -- FILE [args...]`.
scriptArgs[0] is FILE or `<eval>`, followed by the passed argument strings.
The current shell splits on whitespace without quote processing: use a file
for multiline/space-containing source, or a single-token -e expression.
Do not silently join separately passed arguments into executable source.
At most16 script arguments including name,4096 total NUL-terminated argument
bytes,1MiB exact source without embedded NUL. Unknown/missing options reject
before spawning. Source reads use the existing generation-bound Ring-3 VFS
object client, at most5s aggregate with bounded requests, one stable open,
exact EOF and close on every path. scriptArgs[0] is only a descriptive string;
no path authority or file descriptor reaches JS code. Expired admission never
starts a worker; mandatory object cleanup retains the existing bounded1s close.

Parent uses bounded existing private allocation,8MiB ceiling on demand; worker
64MiB process/32MiB engine/16KiB stack/1024 jobs remain unchanged. No eager
whole-budget allocation, allocator/recovery reserve or scheduler changes.
Each existing service command retains its absolute5s deadline,8 nonblocking
packets per poll, generation checks and1s reap deadline. Escape cancels a CLI
wait; there is no transparent replay. Parent failure retains worker orphan
cleanup. Success requires explicit graceful close/reap, not GC alone.

## Private protocol extension and console publication

Existing72-byte header/version1 and operations1..5 remain unchanged. Append
SCRIPT6: one admitted script transaction on a fresh worker after HELLO; no
second SCRIPT or mixing with a previously evaluated browser realm. It carries
a versioned24-byte inner header (version,size,argc,argument bytes,source bytes,
reserved zero), argument bytes, then exact source. Total bound is source1MiB
plus4096+24; frame capacity remains2048. Bounds/identity/sequence/offset/status
and the complete inner request validate before installing console or evaluating.

Console callbacks stage ordered records in worker-private memory: uint32 stream
(1 stdout/2 stderr),uint32 byte length,UTF-8 bytes. At most256 records and60KiB
including record headers. Conversions share the engine deadline and heap;
catching a quota error cannot clear failure or publish a successful prefix.
Recursive console calls during argument conversion fail closed in this profile
(same poisoned-context limit), rather than overwrite an in-progress record.
No callback performs OS I/O. Arguments are installed as data, never eval text.
Ordinary completion/exception replies contain a versioned24-byte header
(version,size,engine status,exit code,record count,record bytes) followed by the
records. Engine status0/1 only; exception maps to exit1. Resource/protocol/
timeout failures use the existing fenced failure path and publish no journal.

Runner validates the entire reply before any output. It routes records through
inherited descriptors1/2, handles short writes within a bounded output deadline,
and replaces C0 controls except newline/tab, plus DEL, with `?` to prevent
terminal control injection. This documented text-console profile is not binary
stdout or shell redirection support. No script-selected descriptor or path.
Output waits until evaluation completes (no streaming or partial-on-timeout).
Diagnostics are bounded host messages: usage64,source66,internal/protocol70,
resource71,I/O74,timeout124,cancel125; normal script-selected codes0..125.

## Frozen proof and scope

Exact files and10 gate groups are frozen in automation/reist-s03b.toml.
Tests first: actual i386 O0/O2 console/core, hostile argument/output envelopes,
no-publication-before-validation, source/CLI/error paths with fake backends,
existing protocol/owner/native Script-domain tests and target links. Preserve
the old core vectors and all source/syscall denial requirements after extraction.
Both reference packages run sequentially. Artifact verifier independently reads
all packaged PRGs and sample hello.js in both actual images, checks Windows/
Make/shell command resolution and unchanged accepted kernels plus BENCHMARK,
MATHTEST,TEXTTEST,CURL,JSTEST. SDK core layout and engine semantics stay unchanged;
new console object is linked only by JSWORK, not JSTEST or ordinary programs.

Real QEMU1024MiB snapshot,headless,180s per guest: ordinary shell runs js -e,
file and arguments; diagnostic fixture covers exception,limits,timeout,cancel,
separated simultaneously living script/browser-style realms and exact reaps.
Existing restricted JS service fault/hang/orphan guest and external browser JS
guest must still pass. No new VMware throughput/WCET claim. No kernel changes
means preserved kernel/benchmark bytes guard the existing performance result.

Logs under build/codex-agent/r335-js-runner, retain failed evidence and old
references. Each frozen gate once, only affected retries after documented
in-scope corrections. No agents, visible windows, Windows crash dialogs or push.
After all gates inspect diff/scope, archive references, transition queue and
commit locally. File/process/admin capability brokers remain later packages.

## Explicitly approved verifier repair, 8 September 2026

The first external browser guest completed all ordered browser/pixel/cache/
cancel/recovery/shell checks, but failed its final worker count. Raw lines327..329
in r335-js-runner/browser.log contain two complete asynchronous diagnostics
inside BROWSER_SCRIPT_FETCH_WORKER: REIST_NETWORK ARP_RESOLUTION_QUEUED and
ARP_RESOLUTION_MEDIATED. All14 actual reaps exist; the broken start is pid30,
generation11. This run is retained as failed evidence, not accepted by retry.

User explicitly authorizes scripts/run_qemu_browser_external.py in scope.
Only these two exact, complete newline-terminated diagnostic strings may be
removed in a bounded derived view for fetch identity/reap matching. No guessed
PID/generation, arbitrary fragment joining, status/count or timeout relaxation.
Raw logs remain untouched; fatal markers in raw or derived views still fail.
Negative regression covers partial/unknown noise, duplicates, missing reaps,
stale generations, bad cancellation and fatal text. Then rerun the same browser
gate on the unchanged images. Other passed runtime/build gates stay valid.

Final fixture audit: process generations are slot-scoped. Simultaneously live
workers require different PIDs and nonzero generations, not different generation
numbers. JSRUNTST checks the actual identity contract; the runner guest starts
it directly from a fresh shell before the first CLI example. Rebuild both
references, compare every packaged program against the prior passed candidate
(only JSRUNTST may differ), and rerun the runner/image gates. Existing service
and browser proofs remain applicable only if all their exact program/kernel
bytes remain unchanged. No production owner/engine code or budget is changed.
