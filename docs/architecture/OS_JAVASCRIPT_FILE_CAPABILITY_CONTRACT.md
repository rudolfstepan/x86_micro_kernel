# JS3 read-capability host: R3.36

Frozen 8 September 2026 on c9bf94ba. Direct main-worktree execution, one
package, no agents/push/visible VMs. This closes the read-only delegation
boundary, not the independent persistent-write protocol still required by JS3.

## Inventory and references

Reuse userspace/js, restricted JSWORK and the existing four-slot Ring-3
vfs_file_client. Open pins a service-owned regular object to client/service/
slot generations; later read/seek/stat/close never resolve its path again.
Native Script workers cannot call VFS or acquire new endpoints. The trusted
runner alone owns VFS objects. Its crash is contained by existing process
cleanup and incremental storage owner reaping. No new kernel or storage ABI.

Reference semantics: POSIX open/read/lseek/fstat/close terminology and byte
offsets, and [QuickJS native classes](https://bellard.org/quickjs/quickjs.html#JS-Classes).
The explicit REIST v1 adapter is not POSIX descriptors, Node fs, File System
Access, synchronous Web APIs, or a directory sandbox. No standard API name is
reused for ambient path access. Language code uses opaque native objects, not
path strings, descriptor numbers or a privileged-mode flag.

Inventory limitation: vfs_file_client exposes only READ/SEEK/STAT/DELEGATE,
and namespace mutation is not a general regular-file write-by-object API.
General writes therefore require a separate Ring-3 object/persistence package:
explicit write rights, stable identity, bounded transaction admission, durable
commit evidence, unknown-outcome quarantine and power-loss/restart tests on
each supported backend. Never use legacy kernel VFS writes as a shortcut.
Directory-root delegation needs a stable directory-relative resolver; it is
not emulated with string prefixes or lstat-then-open checks.

## Authority and interface

`js [--read FILE]... [-e SOURCE | -- FILE | FILE] [args...]` grants up to four
explicit regular files. Parse all arguments before opening anything. Source
admission completes and closes its temporary file before granting the files.
The host copies bounded paths; a failed open closes already admitted grants
and starts no worker. Final symlinks are rejected with existing O_NOFOLLOW.
Intermediate links may be resolved by the trusted grant-time open; this
delegates that exact resolved file, never a subtree or future path lookup.
Reject dot-dot components in grant strings. After admission path replacement,
rename, symlink substitution and service restart cannot retarget the grant:
existing object validation succeeds for the same object or fails closed.

The CLI installs `reist.files`, an array of opaque native objects with
`read(maxBytes)` (ArrayBuffer), `readText(maxBytes)` (independently UTF-8-decoded
chunk, no incremental decoder), `seek(absoluteByteOffset)`, `size()` and
idempotent `close()`. Numeric arguments must already be finite integral Numbers;
no coercion callbacks. No constructors, native tokens, write/open/path APIs or
automatic authority via prototype/JSON/structured data. Browser EVAL and plain
SCRIPT acquire no file bindings. Native class storage is host-owned until
engine destruction; GC is not responsible for closing OS objects.

Host slots carry only READ/SEEK/STAT and a non-reused lease generation. Wire
slot/lease selectors are not bearer secrets: authority is the exact
kernel-delegated endpoint plus checked parent/child generations, outer request
and explicitly granted table entry. Guessing a selector cannot create a grant
or reach another host. JS never receives those selectors as data.

## Protocol and bounds

Keep72-byte v1 header and ops1..6 unchanged. Append CAP_SCRIPT=7 and FILE=8.
CAP_SCRIPT prefixes the unchanged SCRIPT body with a fixed80-byte v1 manifest:
version,size,count,reserved, then four16-byte slot/lease/rights/reserved records;
unused records zero. Validate all bytes before installing bindings.
Only a fresh post-HELLO realm accepts SCRIPT or CAP_SCRIPT once; no mixing EVAL.

FILE is a nested worker-to-parent request only while CAP_SCRIPT is awaiting
its final journal. Same parent/child/document/outer sequence/deadline; strictly
increasing inner call ID. Fixed32-byte request: version,size,call,slot,lease,
operation,argument,reserved. Fixed32-byte response: version,size,call,signed
errno,bytes,value,reserved[2], followed by exact read bytes. No pointers/paths.
Header/identity/sequence/reserved/size errors fence before I/O or publication.
Unknown/ungranted/closed objects and disallowed operations perform no I/O.
Every response is entirely staged and checked before JS observes it.

Four capabilities,256 host calls and16MiB cumulative read bytes per execution;
one read at most128KiB, existing bulk VFS path. Exact EOF and short reads are
valid; unsupported/ambiguous object state is an error, never a path reopen.
One outstanding call, one borrowed response buffer, no asynchronous callbacks
or extra worker endpoints. Existing32MiB engine/64MiB worker/1MiB source and
five-second aggregate execution deadline remain. File open/cleanup each have
an aggregate five-second budget and per-operation maximum one second.

JsSession.poll remains at most eight zero-timeout IPC operations, no VFS.
It exposes a pending host call to the CLI, which explicitly performs bounded
VFS work outside poll and resumes response transport. Browser never services
such calls. Worker uses deadline-bounded directed IPC and revalidates parent;
transport loss, bad replies or quota exhaustion poison evaluation even when
script code catches the immediate exception. Ordinary file errors can be caught.

After success, exception, cancel, timeout or malformed reply: fence/reap worker,
explicitly close all grants, then publish validated console output. No automatic
replay/reopen/regrant. Close uncertainty reports failure and terminates the
trusted runner for normal owner cleanup, not an unpinned kill or silent success.
No writes exist, so this stage claims no persistent transaction/replay semantics.

## Frozen proof

Queue freezes exact files and gates. Regression-first actual i386 O0/O2 tests:
native JS objects/forged receivers/coercion/closure/error/quota, hostile broker
requests and responses, rights/identity/lease/replay/closed/overflow checks,
no I/O before full validation, pinned-handle no-reopen behavior and every cleanup
path. Existing VFS object and JS engine/runner/session/native-domain tests remain.
No source-pattern substitute for behavioral or guest proof.

Both reference images contain normal JS command and sample read script. Read
both images independently; kernels and BENCHMARK/MATHTEST/TEXTTEST/CURL/JSTEST
must remain byte-identical to c9bf94ba. Real QEMU1024MiB snapshot/headless:
normal shell delegation, actual file content/seek/stat/EOF/close, denied ambient
access, quotas/exceptions/deadline and repeated runs reclaiming all four slots;
existing restricted fault/hang/orphan and browser external-script guests pass.
Host compilers <=90s, executables <=30s, guests <=180s, one VM/build group at a
time. Logs under build/codex-agent/r336-js-files; preserve prior references and
failed evidence. Only affected gates repeated after focused in-scope repairs.
After all gates, inspect diff/scope/queue, archive final artifacts and commit.
Next is JS3 persistent object writes, not JS4 or the deferred VMware package.
