# R3.41: bounded FAT32 journal handoff to Ring 3

Design inventory on clean7d87119c, 9 September2026. This is the authority
boundary required before general writable objects, not a JS write grant.
Existing kernel filesystem/journal policy remains explicit migration debt.
Definition checkpoint only: none of the new mechanisms below is implemented
or runtime-qualified by this document. The queue freezes the implementation.

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

Scope correction explicitly approved by the user after the native failing
syscall130 profile regression: include kernel/proc/process.c solely for the
initialize_domain_profile allowlists (Storage allow; Compatibility deny).
The38-file scope retains all17 frozen gates; no dispatcher exception, scheduler
change, or wider domain authority. Existing candidate changes are owned edits
from the clean4f16b53c implementation baseline, not unrelated user changes.

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
