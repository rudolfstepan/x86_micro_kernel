# R3.41: bounded FAT32 journal handoff to Ring 3

Design inventory on clean7d87119c, 9 September2026. This is the authority
boundary required before general writable objects, not a JS write grant.
Existing kernel filesystem/journal policy remains explicit migration debt.
The original definition and diagnostic chronology are retained below. Current
status: accepted on9 September2026 with the explicit historical-risk disposition
in the next section. The unchanged runtime candidate passes all17 current gate
groups and nine additional groups; this is not unconditional assurance.

## Final acceptance and explicit open-risk disposition

The user explicitly approved accepting the current candidate using its passed
tests while retaining two old failures as unresolved risks, after being told
that this would NOT mean those failures were fixed. This amendment supersedes
only the requirement below and in deadline_scope_amendment to demonstrate the
causes of these two historical runs before acceptance. All other requirements,
scope,17 gates, extra deadline/legacy proofs and resource budgets remain intact.

- **R341-H1, open:** resume-arp/guest-wrap/result.json, hang case: a128KiB
  stage5 read was refused before intentional hang injection; quarantine and
  shell prompt followed. Whole-medium comparison shows no stored-byte change.
  The original IO errno was not captured. An intermittent admission/transport/
  completion fault cannot be excluded; the newer deadline repair does not prove
  the cause of this particular run.
- **R341-H2, open:** resume-arp/guest-timer/result.json, normal case: STAT
  reported success but no following shell prompt arrived within120s. The whole
  medium matches the successful transaction oracle, but no exit/wait/UART or
  reader/host-continuity state exists for that failure. A guest liveness fault
  or observation failure cannot be excluded. No standby cause is established.

Original failed records remain failed and immutable. Neither risk is marked
resolved by later passes or by the offline byte audit. On a new occurrence,
stop that acceptance path, retain errno, phase/generation, serial-reader and
host-continuity evidence using the existing bounded diagnostics, and pursue a
focused correction. This disposition grants no automatic waiver for new errors
and no production scheduler/UART repair authority. Ordinary fault containment,
fencing and degraded-state policies remain mandatory.

The interactive acceptance archive at
build/codex-agent/r341-journal-handoff/accepted-final/ binds all17 existing
successful groups and nine additional records to unchanged runtime sources,
tests and reference artifacts. No repeated gate campaign is needed solely for
this documentation/disposition change. Offline forensics remain separately in
historical-audit/result.json. Preserve original failures, prior checkpoints and
the source stash; do not push. This is a research-package acceptance with open
risks, not a hardware durability, universal liveness or fail-operational claim.

## Inventory and chosen boundary

The R3.38 guard already serializes a bounded mutation against legacy VFS and
both open-object registries. Its owner has PID/generation, a nonwrapping token,
resource, namespace epoch and absolute deadline of at most5000ms. Expiry,
owner death and UNKNOWN finish atomically fence before retiring the owner.
R3.39 prevents replacement before actual reap. Reuse these mechanisms.

SYS_STORAGE_BLOCK_WRITE85 still enters the legacy ATA journal. Calling it from
a second journal is incorrect: journal-sector writes are denied and ordinary
target writes may be journaled twice. fat32_prepare_write reattaches only when
the legacy journal is not already bound. fat32_activate restores cached BPB,
FSInfo and directory/cluster cursors; external mutations must invalidate hints.

Chosen handoff is transaction-scoped, not a long-lived volume/file lease. A
new explicit journal mode on the existing mutation reservation excludes every
open object on that volume for the short recovery/qualification transaction.
It must never be used as a lifetime-long pin. Normal object writes will later
use their own stable pin/admission contract; do not silently release/reopen a
file or broaden old READ/DATA/ALL rights to get past this boundary.

## Frozen requirements for implementation

1. Add an append-only EXTERNAL_JOURNAL mode to FILE_OBJECT_GUARD begin. Require
   a single normalized FAT32 key plus EXCLUSIVE, current live Storage owner,
   exact epoch, no conflicting legacy node/pin and existing5000ms deadline.
   Preserve the112-byte request and old begin wrapper. Retain mode in protected
   fixed-size metadata (at most64 bytes), reset it only with owner retirement.
   Old BLOCK_WRITE/FLUSH must not work in this new mode. New mode authorization
   requires the exact mutation token as well as service/resource/generation.
2. Under VFS -> ATA mutex ordering, retire only a quiescent legacy binding:
   no pending entries or nested journal transaction may be discarded. Failure
   has no device effects and no lost reservation. Invalidate FAT/FSInfo hints
   and per-context cursor generation through the filesystem adapter. Do not
   add a kernel pathname parser or journal-format interpreter. The existing
   fat32_prepare_write reattaches/revalidates before subsequent legacy writes.
3. Append syscall130 STORAGE_JOURNAL_IO; old0..129 and wrappers stay intact.
   Version1 request is32 bytes: version, size, operation, token, resource,
   sector, count, one zero reserved word. Separate second argument is a bounded
   data buffer, null for flush. READ1/WRITE_DEFERRED2 transfer1..256 logical
   512-byte sectors (at most128KiB); FLUSH3 requires sector/count zero. This is
   a REIST capability mediator, not a POSIX
   raw-device compatibility API. Whole structure and directional user ranges
   are checked before effect; all unused fields must be zero. Stage complete
   input before effects in fixed kernel-owned storage; no large task-stack
   object or heap allocation in the IO path. Arithmetic is checked before
   multiplying count by512. WRITE_DEFERRED completion is not durable completion.
4. Admit every IO under the same VFS lock and exact live reservation; never use
   maintenance/unmounted fallback. Normalize a partition exactly once and
   reject parent aliases, another volume, stale tokens, expiry and generation
   changes. Preserve existing device fencing/supervision, bounded PIO operation
   and full readback. Retain controller limits and bounded batching; do not
   implement a bulk ABI as per-sector writes with implicit per-sector flushes.
   No device port/MMIO/DMA authority reaches Ring3. Initial
   qualification is ATA-PIO, covering default IDE QEMU/VMware images. AHCI/FDD
   remain ENOTSUP in this new mediator until their separate hardware proof;
   their existing APIs and behavior must remain unchanged.
5. The Ring-3 FAT32 transaction adapter owns the existing transport-neutral
   RSTJ core from R3.40 in fixed instance storage. It receives explicit volume
   geometry and the admitted token; no ambient fallback. All reads/writes and
   barriers use the token-bound mediator. Preserve the four journal durability
   barriers (undo, ACTIVE, targets, CLEAN) with explicit deferred batches and
   FLUSH. Device supervision must not claim durability before successful flush;
   no kernel mutex ownership may survive a userspace return. Track attempted effects, distinguish
   NO_EFFECT/DURABLE_COMMIT/UNKNOWN, never turn an uncertain result into retry
   or an implicit grant. Reject NO_EFFECT finish after an attempted write and
   reject DURABLE finish with an outstanding unflushed batch; invalid finish
   retains the reservation until explicit UNKNOWN cleanup or deadline fencing.
   Host callbacks cannot call SYS_WRITE or old raw writes.
   Preserve v1/v2 formats,20-slot transaction capacity and reverse recovery.
6. The new adapter is qualified with private Storage test builds, not exposed
   as a new userspace command or JS method. Real requests enter through the
   existing userspace STAT command. Normal production image must have no test
   trigger, fault/hang switch or write request delegated to ordinary clients.
   General writable-object API, bulk input/growth/truncate/append planner and
   JS bindings are subsequent work. Do not claim default application writes
   have migrated merely because the new mediator is qualified.

## Failure and acceptance

Native regression first: old guard rejects the missing new mode; after change,
actual guard/VFS/adapter behavior must cover capacity, current/stale owners,
tokens/resources/partition aliases, legacy mixed access, malformed pointers,
no-effect cancel/copyout, retirement and deadline fencing. Test actual journal
commit and recovery at every write/barrier interruption, not a source proxy.
No new heap or large stack object; no metadata lock held across IO.

Real headless1024MiB QEMU uses private standard-size FAT32 auxiliary disks and
private Storage fault builds. Prove normal Ring-3 journal writes/recovery,
legacy read/write before/after handoff, no mixed writer, rejected stale/foreign
calls, exact whole-disk outcomes, process fault/hang/reap and persistent UNKNOWN
fencing across replacement. Independent root volume/shell must survive.
Do not claim a fenced uncertain volume automatically becomes writable again:
explicit Ring-3 requalification and immutable-object reintegration remain
requirements of the subsequent backend. Never clear a fence merely to pass.

Both reference builds, actual image kernels, unchanged protected applications,
normal journal/scheduler/CPU-local source paths and existing JS-file/browser
guests are required. Exact artifact/guest/native gates and allowed files are
frozen in automation/reist-s03b.toml before candidate edits. Keep all failures,
bounded execution and ignored evidence; no nested agents, visible VMs or push.

## Direct implementation order and code anchors

Resumption on clean996cff15 after the independent R3.41a ARP acceptance:
restore the owned stash c8cd59ea26548e97ad1a7e9ef69a9a4d4421f22a without
dropping it. Reconcile CURRENT_WORK separately; do not overwrite the current
queue or accepted network service. Rebase only the protected REIST.PRG hash
to that accepted commit. All other payload rules and all17 gate groups stay
frozen. Renew final builds and guest proofs in new evidence subdirectories;
preserve the prior16/17 candidate, failed browser gate and all diagnostic runs.

Scope correction explicitly approved by the user after the native failing
syscall130 profile regression: include kernel/proc/process.c solely for the
initialize_domain_profile allowlists (Storage allow; Compatibility deny).
The38-file scope retains all17 frozen gates; no dispatcher exception, scheduler
change, or wider domain authority. Existing candidate changes are owned edits
from the clean4f16b53c implementation baseline, not unrelated user changes.

Second scope correction approved by the user on9 September2026 after the
R3.40 compatibility guest rejected the actual concatenated shell startup:
include scripts/run_qemu_fat32_recovery_admission.py and
test/test_fat32_recovery_admission.py solely for exact startup-log admission
and its regression. Preserve all17 gates, whole-disk oracles, time limits and
the original failed evidence. The scope is now40 files; this is not approval
for network/supervisor changes or acceptance of the intermittent browser
download failure. Existing candidate edits remain owned, uncommitted work.

Third scope correction approved on9 September2026: include
test/test_reist_probe_domain.py to compare the process-domain syscall limit
against the central ABI count, preserving profile2, bitmap5 and all existing
authorization checks. Run its nine tests as mandatory additional evidence;
the original17 groups remain frozen. Scope is now41 files. This does not
authorize scheduler, wait/exit or PIO-hotpath changes. The32 owned candidate
paths were verified against the unaccepted checkpoint before resumption.

Private failure diagnostics now retain the first raw-IO errno in the fixture's
STAT create_time field (positive errno, not a real file timestamp), while size
continues to identify the stage. No extra timing syscall occurs inside the
transaction, and this witness is absent from production Storage. On a20s guest
wait, the handoff checker may request read-only HMP register/PIC/block statistics
before continuing to the same absolute deadline. These records do not replace
the required prompt, media oracle, generation/reap proof or failed gate result.
Neither diagnostic success nor later unreproduced runs resolve the earlier
stage5 refusal and missing-prompt failures without a demonstrated cause.

Fourth scope correction approved by the user: private-test instrumentation of
kernel/sched/scheduler.c, drivers/char/serial.c and the existing syscall_wait
section of kernel/syscall/syscall_table.c. Scope43, original17 gates and the
additional domain test unchanged. Fixed exit/wait phase fields and serial-drop
counters only; no text logging, dynamic allocation, new API/authority, deadline
enlargement or recovery action. Generate instrumented copies under ignored
evidence, keeping the production sources untouched. Exact source anchors must
fail closed on drift. Preserve original __FILE__/__LINE__ with line directives;
link a no-define control and require byte-identical production kernels before
using the private REIST_JOURNAL_HANDOFF_TRACE build. Counters are modulo2^32;
snapshots are diagnostic observations, never synchronization or authority.
This approval does not authorize a production scheduler, wait or UART fix.

The host checker now qualifies observation continuity independently of guest
progress:100ms samples, a1s maximum gap on monotonic and wall clocks,4096
samples and a bounded1s join. A gap, backward/non-finite clock or observer
failure makes the run fail; a coordinating-thread check prevents accepting a
buffered prompt before the sampler resumes. This does not detect every host
disturbance or prove the cause of a guest failure. No automatic retry, timeout
extension, new guest authority or replacement for the existing media/lifecycle
oracles. A controlled7s observer/VM interruption must fail; uninterrupted
normal/hang controls retain the complete existing assertions. The user's
probable-standby report does not retroactively qualify the old runs: Windows
power events inspected for9 September do not overlap13:59--14:07. Preserve the
original failures and cause requirement above.

Serial observation is separately fail-closed: unexpected reader EOF, read/log
failure, queue exhaustion or the existing2MiB quota invalidates the run. Record
bounded error text and the reader's completion event; waits check it before
and after at most250ms slices, and before further command injection. The20s
read-only diagnostic point and absolute guest deadlines remain unchanged.
Only EOF following explicit verifier cleanup is expected. Buffered prompts
cannot qualify a run whose reader has already ended. A transport-loss test at
the post-STAT boundary plus a healthy fault/restart control validates this
checker correction, not the root cause of a historical unobserved failure.

### Newly demonstrated deadline gap; targeted correction approved

At the failing checkpoint the cold ATA mediator received no reservation
deadline. After guard admission it could wait on the existing10000ms ATA mutex
or start another batch after expiry; guard completion fenced only afterwards. The
actual native guard and extracted ATA mediator demonstrate late read/write/
flush callbacks after a simulated lock delay, and late batches after the first
valid write. Both O0/O2 reproduce this with a5000ms reservation and with a
valid1ms reservation. Physical transport and time progression are mocked:
this is a native boundary proof, not a reproduction of the historical guest
failures or a hardware/WCET result. A timely control passes.

This violates pre-effect authority validation. The previous16/17 recorded
gate successes do not qualify the candidate despite those tests not exposing
this interleaving. Preserve the red evidence under wait-diagnostic/deadline-
probe-2/ and deadline-probe-short/. The user subsequently explicitly approved
the targeted shared ATA-PIO correction. This is the sole exception to the
PIO-source-unchanged requirements above and below, not permission to change
journal policy, scheduler, UART, AHCI/FDD or general driver behavior. All34
owned checkpoint files matched before resumption; scope remains43 files.

Required correction: carry the exact protected reservation
deadline through VFS admission, ATA lock waits, selection/readiness waits,
batch submission, readback and flush. Revalidate after waits and before fresh
commands; never derive a new reservation from elapsed time. Preserve old call
contracts, existing device limits, fixed staging and the four barriers. An
already attempted write that cannot finish durably remains UNKNOWN/fenced;
expiry grants neither replay nor automatic reintegration. Reuse existing
bounded transport mechanisms, not another filesystem/driver implementation.
Add native lock/readiness/batch-expiry regression and an actual guest proof;
retain the original17 gates and domain test, and qualify unchanged legacy
semantics and performance. No scheduler, new syscall or JS grant is needed.

Retain old transport signatures through deadline-free legacy wrappers. The
new internal path gets the protected deadline, never a fresh user-selected IO
lease. Reuse kernel_mutex_lock_until and existing bounded PIO status waits.
Preflight may briefly read guard metadata before waiting for VFS, but must
release that metadata lock before VFS acquisition and revalidate afterwards;
the VFS -> metadata/ATA ownership order is unchanged. Timeout cannot publish
a token or a successful durable result. Normal T13 ATA command selection,
sector units, range limits and cache/flush semantics stay intact.

The queue's deadline_scope_amendment freezes extra existing ATA/benchmark
tests, native command-level expiry cases and a real guest expiry proof.
Reference/candidate QEMU benchmark runs use identical1024MiB configurations;
retain results without claiming VMware throughput or hardware WCET. Artifact
comparison exempts only the explicitly enumerated deadline helpers, checks
all other ATA/journal/scheduler source unchanged and tests exemption negatives.

The correction candidate now uses those deadline-aware paths and retains the
old legacy entry wrappers. Native O0/O2 regressions cover1/5000ms reservations,
lock resumption, selection/readiness, register preparation, read/write batches
and flush, including LBA28/LBA48 and unchanged legacy command/barrier counts.
The register-preparation negative exposed a second check needed immediately
before command issue; its red run is retained separately.

The private signed guest suspends one admitted128KiB read under the ATA lock
until its real deadline. A fixed20-byte witness records expiry, zero actual PIO
commands and refusal. Its100ms reservation is valid under the unchanged5000ms
maximum and avoids conflating the outer STAT RPC timeout with IO expiry. The
first5000ms fixture was unqualified, not accepted as a runtime proof. Generated
source preserves original line mappings; the no-define kernel is byte-identical
to production. The successful case also proves whole-disk equality, quarantine,
exact retired/fresh service identities, fence retention after manual restart
and independent root/shell liveness. Evidence: deadline-repair/expiry-guest-
monitor-fixed/result.json. No production fault trigger or caller deadline change.

The new HMP witness exposed a verifier-only prefix case: one exact `(qemu) `
prompt may precede a complete Storage lifecycle record, optionally after the
shell prompt. Accept only that grammar with original offsets, never arbitrary
prefix stripping, partial lines or repeated prompts. Positive/negative tests
cover this; normal/fault/hang assertions and host continuity requirements stay
unchanged. Earlier failed observations, including a wall-clock discontinuity,
remain unqualified evidence. None of this demonstrates the cause of the two
historical uninstrumented guest failures; their acceptance restriction remains.

1. Actual native guard regression, then protected mode/effect/barrier state in
   kernel/init/file_object_guard.c and include/kernel/file_object_guard.h.
   Preserve old entry wrappers; vfs_file_object_guard_request/io_begin are the
   authority/normalization adapters, not a second global lease registry.
2. Freeze/generate the append-only ABI in include/reist/abi/syscall.h and its
   SDK copy, generator, libc wrappers and process-domain limit. The syscall
   implementation validates both user ranges before taking IO authority. The
   native Script domain and every non-Storage process must still deny it.
3. Add only cold, bounded ATA-PIO handoff/deferred transport hooks. Reuse
   ata_journal_write_sectors_deferred_transport, existing command bounds and
   flush/readback machinery. Do not change normal PIO/journal hotpaths.
   fat32_prepare_write already reattaches an invalidated journal binding;
   adapter context invalidation must cover FSInfo and data_generation too.
4. Add userspace/storage/{include/reist,lib}/fat32_transaction.{h,c} and reuse
   drivers/block/ata_journal.c, not a copied second journal implementation.
   Qualify the real adapter/core with injected native IO and private Storage
   builds. Keep normal Storage requests read-only until the object backend is
   implemented. Private test image rebuilding must preserve reference images
   and validate replacement program capacity rather than truncate a larger PRG.
5. Execute the frozen17 groups serially by compiler/build/VM group, retain
   failures and final images, verify scope/hotpaths and queue transition, then
   commit. Do not add a new user command merely to invoke this authority.

Native compiler <=90s, native executable <=30s; new guest proof aggregate <=360s
and <=120s per case. Existing guest deadlines are unchanged. The new guest must
contain at least normal, fault and noncooperative-hang cases; each must prove
exact media effects plus independent root/shell liveness. Test read-only return
and mixed-API rejection without artificially opening unrelated write rights.

## Next unified writable-object implementation

After this authority boundary, implement fixed-size writes, append, growth,
truncate and explicit fsync together for the default FAT32 backend. Reuse the
existing FAT allocator/directory algorithms behind Ring-3-owned callbacks;
validate complete transaction capacity before publication and report bounded
partial progress for resources spanning multiple transactions. A stable owned
file pin must coexist with short mutation admission without pathname reopen.
Add explicit recovery/requalification before writable reintegration; only then
delegate opaque JS write capabilities. FAT12 has a different remap/journal
protocol and EXT2-only support is not a substitute for default FAT32 images.
