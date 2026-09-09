# R3.42: Ring-3 FAT32 writable objects and requalification

Definition on clean `3e7c02add995b5c33aed045913382dc7ccce2d0f`, 9 September 2026.
Status: implementation contract, NOT implemented or runtime-accepted.
This is the next JS3 backend slice. R3.41 remains accepted with unresolved
historical risks R341-H1/H2; their disposition is not a waiver for new failures.

## Scope and references

Implement one cohesive object/transaction boundary: explicit writable handles,
validated bulk input, overwrite, append, zero-filled growth, shrinking, sync,
and recovery before writable reintegration. First backend: existing regular
files on the default ATA-PIO FAT32 images. Include fragmented files, partial
sectors, empty files and supported FAT32 cluster/mirroring variants together.
No new file creation, namespace authority, FAT12 remap protocol, EXT2 data-write
backend, AHCI/FDD qualification, persistent format or JS write binding here.

Use POSIX.1-2024 terminology for byte offsets, write/pwrite, append, short
writes and fsync. Preserve the distinction between transferred bytes and
durable completion. The standard's
[file-operation atomicity rules](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_09_07)
are a reference, not a compatibility claim. A deliberately incremental resize
is called `resize_step`, NOT `ftruncate`: an error may follow a reported durable
size change. This versioned REIST deviation avoids both a hidden partial
truncate and a new persistent orphan-intent format. No POSIX/Node fs claim.
FAT geometry, cluster values and directory fields retain the existing Microsoft
FAT on-disk interpretation; RSTJ v1/v2 and the existing20-entry journal remain
unchanged. ATA commands, sector units and durability barriers remain unchanged.

## Inventory: reuse and exact gaps

| Boundary | Reuse | Required change |
| --- | --- | --- |
| Object lifetime |16 service slots, four per client, stable canonical key, kernel pin, generation-scoped close/reap | Admit a transaction with its exact owned pin still present; all other conflicting pins/nodes continue to exclude it |
| Request transport | Eight requests, two per client; two128KiB kernel bulk buffers; cancellation/copy quiescence | Stage complete client input before claim; service takes immutable bytes bound to the request and both owner generations |
| Transaction | R3.41 external-journal token, maximum5s absolute deadline, deferred/readback IO, four flush barriers | Bind normal mutation to the claimed request and owned pin; add no maintenance/raw-write fallback |
| FAT32 backend | Ring-3 geometry/locator parser and cache; legacy cluster/directory algorithms; transport-neutral journal core | Ring-3-owned transaction planner, allocation/cursor validation and post-commit locator refresh, without legacy global-state shims |
| Reintegration | R3.39 retire/reap order, sticky uncertain-media fences, media fingerprints | Repair-only admission while ordinary writes remain fenced; fresh generation and verified recovery before any writable publication |

Code anchors: `vfs_object_*` in userspace/programs/storage_service.c;
userspace/storage/lib/{vfs_file_client,vfs_shadow_fat32,fat32_transaction}.c;
kernel/init/{storage_request_pool,file_object_guard,storage_service,
storage_safety,filesystem_safety}.c; existing VFS and ATA external-journal hooks.
Read legacy fs/fat32/{fat32_cluster,fat32_files,fat32_vfs_adapter}.c for reuse,
but do not move their process-global boot sector, allocator or journal state
into a second owner. No new filesystem parser or recovery policy in Ring0.

Specific hazards already found:

- EXCLUSIVE currently rejects the writer's own lifetime pin. Dropping/re-pinning
  it or reopening a path is not a repair.
- Existing bulk transport only publishes service output. Passing a client's
  pointer to Storage, or claiming before input copy completes, is invalid.
- FAT32 locator validation includes the start cluster. Only this object's
  verified commit may refresh that locator when empty/grown/shrunk-to-zero;
  accepting a foreign entry change is not cache invalidation.
- Current parser limits (320 reads,6400 file-chain steps) are per-operation
  safety bounds, not a writable file-size contract. Add bounded resumable
  cursors; do not remove all bounds or silently report EOF at these limits.
- Current requalification checks media identity but does not clear an object
  fence. The existing polling path intentionally skips fenced resources.
  Neither a fresh PID nor `accept_formatted_media` authorizes recovery.
- Clearing global ATA/filesystem fences temporarily to repair one volume could
  authorize unrelated writes. Repair transport must remain a separate mode.

## Implementation order inside this single package

### 1. Freeze executable negatives and append-only ABI

Add actual O0/O2 regressions before the corresponding production change.
Keep syscalls0..130 and count131, old wrappers, old112-byte guard requests,
old512-byte object frames and descriptors v1/v2 valid with their old semantics.
Extend existing versioned mediators/operation namespaces; no new syscall is
needed. Add explicit new object operations for writable open, mutation and
attenuated delegation. Old OPEN/OPEN_RIGHTS/OPEN_FLAGS and old READ/SEEK/STAT/
DELEGATE masks DATA=7, ALL=15 never acquire write rights.

The extended API distinguishes overwrite, append, resize and sync authority;
append-only objects cannot overwrite or shrink. New rights are explicit bits,
not a widened old ALL constant. Reject unknown versions/flags, nonzero reserved
fields, conflicting modes and arithmetic overflow before state publication.
Define new fixed-size structs with compile-time size/offset checks in the
central ABI/SDK and behavioral marshalling tests. Use a new descriptor version
to expose the kernel's original request deadline, never a client-renewed lease.
Keep old ABI/domain tests, including default Script denial, mandatory.

### 2. Complete input and owned-pin admission

Add client-to-service bulk transfer using the existing two128KiB slots, not a
third unbounded staging store. A submitted write is unclaimable while input is
incomplete. Copy and CRC publication bind request handle, client/service
generations, length and direction; bind the resolved media identity at owned-pin
admission. Reject double publish/take, crossed output
buffers and stale handles. Cancellation during copy quarantines that slot until
the copier is quiescent; it must never expose a successor's bytes. Exhaustion
returns a bounded error without a partially executable request.

An extended guard admission identifies the claimed request and exact owned pin,
canonical key, current client and Storage generations, media and namespace epoch.
Only that one verified pin is exempted from exclusive conflict checking. A
second pin, including another pin of the same client, is not exempt. Keep the
pin for the full object lifetime; the exclusive reservation lasts only for one
transaction, at most min(original request deadline, now+5000ms). Other-volume
objects are unaffected. No renewable operation or volume lease.

Validate cancellation/retirement before admission and after waits at every IO
boundary. Preserve the existing VFS -> metadata/ATA lock order and exact deadline
through command issue/readback/flush. No lock survives a userspace return.
Protected records remain <=64 bytes; extra fixed metadata needs its own checked
record, not a larger critical-record limit. Cancel before effects is NO_EFFECT;
cancel/lost reply after possible effects is UNKNOWN and fenced, not a retry.

### 3. One bounded FAT32 transaction planner

Add userspace/storage/{include/reist,lib}/fat32_file_write.{h,c}; reuse the
existing FAT32 parser and transport-neutral RSTJ implementation through the
transaction adapter. Do not copy the journal or fall back to SYS_WRITE.
Before each effect, validate the complete transaction: stable object, immutable
input, checked offsets, chain/range/loop integrity, allocation ownership,
mirrored FAT policy, unique target sectors and journal capacity.
Preserve high FAT bits and untouched directory fields; FSInfo is a hint, not
allocation authority. Corrupt/cross-linked chains, ambiguous mirror state and
unsupported geometry fail closed. Reuse epoch-bound validation/cursor caches;
never trust an allocation certificate after another mutation/media generation.
Any ownership scan is bounded/resumable and in Ring3, not a repeated unbounded
whole-volume scan in the kernel or per data chunk.

Overwrites preserve bytes outside the requested range, including sector tails.
Append chooses EOF under the same reservation as publication; pwrite uses its
explicit offset and does not change the client's seek position. Zero-length
write has no allocation or size effect. Offset/growth gaps and new visible
cluster bytes are zero-filled before publication. Free clusters never expose
previous file data. Distinguish actual ENOSPC from workspace/deadline exhaustion.

Transactions account for data, both relevant FAT copies, directory entry and
FSInfo targets before writing. Split at the actual20-target capacity, not an
arbitrary file-size ceiling. Preserve undo -> ACTIVE -> targets -> CLEAN flush
barriers and full readback. Coalesce contiguous IO where the existing core
permits; no implicit flush for each sector. Only successful durable completion
refreshes cached size/start cluster/offset. Mixed legacy IO remains excluded
during reservation and legacy hints are invalidated before the next owner.

### 4. Explicit long-resource and outcome semantics

Each reply carries a versioned result: request correlation, errno, durable byte
count, resulting size, and NO_EFFECT / DURABLE_COMMIT / UNKNOWN outcome. A
durable prefix before an uncertain suffix is recorded separately; it never
asserts that the uncertain suffix had no effect. Lost/malformed replies cannot
be reconstructed as success from a local timeout. No automatic mutation replay.

Provide write/pwrite/append step operations and `resize_step(target_size)` plus
bounded client convenience loops using one original deadline. Every completed
step is independently recoverable. A long append may interleave with another
operation between steps; no whole-resource atomicity claim. The caller must
receive short progress and can explicitly continue with a new request, not an
automatic reset of its time budget. Seek alone never grows a file.

Growth publishes only durably zeroed/initialized data. A write beyond EOF first
uses explicit growth progress when the entire gap cannot fit one transaction;
do not hide a changed size behind a zero-byte ordinary write failure. Shrink
works from the tail, publishing each smaller valid size with its corresponding
FAT releases in the same transaction. It must not unlink a whole long suffix
and then forget its unreachable clusters after a crash. Truncate-to-zero is
the final recoverable step. No new orphan log or implicit partial ftruncate.
Support large resources beyond the old parser walk limit with bounded windows,
not a one-megabyte exception. FAT32's32-bit file-size limit remains explicit.

`fsync` is an explicit checked durability boundary for this live object and
backend, not a new grant or a no-op that conceals prior failure. Mutations
already acknowledged durable need not rewrite their data. No dirty deferred
writeback at close/GC; close/revoke is idempotent and never silently commits.

### 5. Recover while fenced, then qualify fresh objects

Normal requests remain denied on the uncertain resource. Use the existing
supervisor/restart state machine for both automatic and manual recovery: retire
the exact old generation, fence/revoke, reap, recreate, self-test, then qualify.
Old handles and revoked mounts stay stale; clients close/unmount and acquire
fresh mounts/objects only after qualification. No implicit regrant or remount.

The current Storage generation may obtain a short repair-only token when the
previous owner is quiescent/reaped, no conflicting nodes/pins exist and the
original resource extent, medium fingerprint and journal identity match.
Retain the kernel-owned resource/extent revocation record across unmount so
repair never requires reviving a revoked mount or accepting a caller's range.
Keep ordinary raw writes and normal object mutations fenced throughout repair.
Only token-bound ATA-PIO recovery IO is admitted under the original bounded
deadline. The kernel mediates extents/generations/effects; Ring3 validates all
existing journal before-images before any restoration, then applies the
existing RSTJ recovery algorithm and verifies CLEAN/readback/flush.

Reintegration requires successful journal/geometry/allocation self-test, stable
media identity, no pending write, and generation-matched publication at every
layer. Separate fixed qualification metadata may record this handshake; it
must not enlarge the existing64-byte Storage control or grant policy to Ring0.
Never clear another unsafe volume's fence or a sticky integrity-corruption
fence. A partial publication failure refences before admitting any ordinary IO.
Recovery fault/hang/expiry keeps quarantine and consumes the existing bounded
restart budget; it cannot create an endless sequence of new5s repair leases.
Conflicting/corrupt evidence needs intervention, not formatting or blind replay.

### 6. Prove, inspect, archive and commit

Implement FWRITEST.PRG as an explicit file-object exercise on a caller-selected
test file, packaged in both Windows and Makefile images and resolved by the
normal Ring-3 shell. It has no raw/recovery authority. Private service fault
hooks live only in generated test images, not release opcodes or magic paths.
Keep native Windows errors/dialogs suppressed by the existing bounded runner.

Frozen commands and allowed files are in automation/reist-s03b.toml. Required:

- Native O0/O2 actual client/pool/guard/VFS/planner/journal/requalification code:
  sizes/rights, copy/cancel races, stale identities, all target/barrier/recovery
  cuts, exact whole-media old-or-step-final oracles, capacity/overflow, malformed
  media, fragmented/empty/long files and bounded allocation/IO counts.
- Normal VMware then QEMU reference builds,1024MiB; independently match image
  kernels and every payload. Against the accepted R3.41 archive only STORAGE,
  new FWRITEST and the enumerated existing file-client consumers may differ.
  Consumer changes must be attributable to the shared client ABI/library; their
  application sources and feature rights stay unchanged. Protect BENCHMARK,
  CURL, JSTEST, JSWORK, REIST and all non-consumer payloads byte-for-byte.
- Headless QEMU normal overwrite/append/growth/shrink/sync, long fragmented
  resource and shell dispatch; real owner/service fault, noncooperative hang,
  cancellation/lost reply, stale handle reuse, interrupted recovery and exhausted
  recovery. Exact disk oracle and independent root/shell liveness in each case;
  fresh objects only after qualification, corrupt evidence never unfenced.
- Existing journal/retirement/FAT32 recovery, JS-file, restricted worker and
  external-script browser proofs remain mandatory. A library rebuild is not a
  new JS grant. Retain original failures and stop on a new acceptance failure.
- Compare one accepted baseline and one candidate QEMU benchmark at identical
  settings, with unchanged BENCHMARK.PRG; retain raw times and deterministic
  batch/flush/cache-work counts. No new GUI synchronous RPC, scheduler/CPU-local/
  framebuffer change, or general ATA hotpath rewrite. Enumerate cold recovery
  hooks in the artifact checker, with negative tests against broad exemptions.
  Investigate a material slowdown before acceptance; do not repeat unchanged
  runs until a favorable timing appears or call one VM pair hardware evidence.

The17 existing direct file-client consumers, frozen from the baseline build
manifest, are JS, JSRUNTST, CAT, CHKDSK, BASIC, DESKTOP, NOTEPAD, BROWSER,
IMAGEVIEWER, CONTROL, MOUSE, DISPLAY, COPY, HTTPD, EDIT, GTEST and OBJGDTST
(.PRG each). This is a link-dependency exception only, not authority to edit
their application sources or silently remove protections from other programs.
Record each actual changed payload and the linked-library cause. The new guest
also proves expired/cancelled mutation authority issues zero fresh PIO commands;
UNKNOWN after an earlier effect still requires recovery, not NO_EFFECT.

Host compiler <=90s, individual native executable <=30s. New guest cases <=180s,
new object/recovery campaign <=1080s, benchmark pair <=360s; inherited guest
limits stay unchanged. Run one compiler/build/VM group at a time. No agents,
visible VMs, external media, destructive cleanup or push. No unverifiable
performance, hardware power-loss, WCET, certification or JS-completion claim.

Only after all frozen groups pass: inspect scope/ABI/cleanup and final diff,
archive evidence, mark R3.42 done and perform the existing next-queued transition,
then local implementation commit. The VMware deferral remains binding even if
R3.6b becomes formally active. Subsequent explicit JS write delegation requires
its own host-authority package; do not implement it in this run.
