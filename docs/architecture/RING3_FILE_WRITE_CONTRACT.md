# Ring-3 writable file objects: JS3 prerequisite and implementation order

Inventory on eb4dcc12, 8 September 2026. This distinguishes requirements from
implemented authority. R3.36 provides read-only JS objects. **No writable JS
object, general Ring-3 file write API or new disk format is accepted here.**

## Existing mechanisms and gaps

| Boundary | Existing implementation | Required before writes |
| --- | --- | --- |
| JS worker | Native Script restriction; opaque read objects; generation-bound directed IPC | Preserve default deny; explicitly delegate write objects only in trusted host |
| VFS object client | Four slots, READ/SEEK/STAT/DELEGATE, open_flags accepts NOFOLLOW; later calls carry no paths | New append-only explicit write right, operation and validated input transport; old ALL/DATA must not silently gain rights |
| Service objects | STORAGE.PRG owns16 slots, four per client, owner reaping, service/slot generations | Lifetime pin against unlink/reuse, media generation, serialization and uncertain-outcome fencing shared with every mutator |
| FAT12/FAT32 | Ring-3 read parser; general file mutation and journal-v2 integration still in legacy kernel paths | Move policy/parser/transaction ownership to Ring3 behind bounded mediation; no SYS_WRITE fallback |
| EXT2 | Ring-3 regular object reads and bounded namespace mutations; fixed26-sector undo journal v1 | General data/growth/truncate planning, allocation validation and open-object exclusion are not supplied by namespace operations |
| Recovery | ACTIVE/COMMITTED/CLEAN,24 before-images, CRC/readback, device quarantine mechanisms | Durable terminal states must never be silently reinterpreted as rollback authority |

Code anchors: `userspace/storage/include/reist/vfs_file_client.h`,
`userspace/programs/storage_service.c` (vfs_object_* and vfs_namespace_mutate),
`userspace/storage/lib/vfs_shadow_ext2.c` (ext2_object_inode,
ext2_journal_recover, ext2_regular_allocations, ext2_namespace_commit),
`kernel/init/storage_request_pool.c`, `kernel/init/storage_service.c` and
`kernel/syscall/syscall_table.c` (bounded transport/domain validation).

The EXT2 journal is not a ready-made general file-write adapter. Its recovery
requires one or two publication sectors; namespace planners validate specific
allocation/layout cases. Existing object revalidation checks volume signature,
inode number and generation; this alone is not proof of safe writable lifetime
against every namespace/legacy mutation or recycled on-disk generation.
Do not expose journal sectors, raw device numbers or path prefixes to scripts.

## R3.37: frozen prerequisite repair

Source inventory found: after validating current sectors as old or final,
ext2_journal_recover cleans COMMITTED only when all sectors are final, but
otherwise falls through to ext2_journal_restore. A committed transaction with
contradictory target evidence can therefore be undone as though still ACTIVE.
Durable COMMITTED was written only after final data/publication flush and
readback. Old target sectors at that point are corruption/ambiguous evidence,
not authorization to restore an acknowledged old state.

Repair the terminal-state decision without changing v1, selection rules,
sector validation, ABI, parser budgets, kernel, JS or authority:

- ACTIVE retains the existing bounded undo path.
- COMMITTED with all final target CRCs may only clean the journal headers.
- COMMITTED with any non-final target, missing/corrupt before-image or
  conflicting valid headers at the same sequence/state returns EIO; no write
  or flush is permitted. Different legal cleanup states retain v1 selection.
- CLEAN remains a no-op. Repeated cleanup and interrupted header cleanup
  remain bounded and idempotent. No hidden retry of the original mutation.

An error makes the current Ring-3 EXT2 access fail closed; this package does
not claim a new persistent kernel quarantine bit, controller qualification,
general service-crash rollback or repair of arbitrary corrupt sectors.
An unchanged corrupt journal must continue refusing operations after service
restart. A healthy independent volume and userspace shell must remain usable.

Frozen files and11 exact gate groups are in automation/reist-s03b.toml.
Regression first: capture real production COMMITTED writes before CLEAN;
exercise all-final, all-old/mixed targets, corrupt before-images, conflicting
headers, ACTIVE rollback, cleanup cuts and second recovery. Actual native O0/O2,
no source-pattern substitute. Guest injects valid and contradictory committed
images, proves exact disk effects, real service restart and normal shell.
Existing namespace power-loss host matrix and QEMU namespace/JS-file guests
remain mandatory. No compiler/runtime dialog; host compiler <=90s, executable
<=30s, guest <=180s, one build/VM group at a time, 1024MiB headless snapshot
system image. Only private evidence disks may be changed by the guest.
Both reference images and inherited protected hashes must pass. Preserve
failed runs, archive final evidence, inspect exact scope, then commit locally.
No later implementation package in the same run.

R3.37 acceptance: all11 groups passed; contract checkpoint868ca285, exact
commands/times and red-before-green evidence in CURRENT_WORK.md. Both real
guest cases pass across service restart; contradictory disk unchanged,
coherent cleanup changes only two headers.105 reference files archived.
Only STORAGE.PRG changed among92 programs; both kernels unchanged. The next
section remains future requirements, not an implemented write API.

## Next cohesive write slice (requirements, not an active package)

Use POSIX terminology for open, write/pwrite, lseek, fstat, ftruncate and
fsync: byte offsets/counts, explicit access rights, errors versus short writes,
and data transfer distinct from durable completion. The REIST adapter is not
POSIX/Node fs compatibility. Freeze exact append-only wire definitions and
backend bounds only after the complete mutation/lifecycle inventory; do not
reuse an existing opcode with different semantics.

1. Choose the first supported persistent backend explicitly. The normal images
   are FAT-based; EXT2-only proof must not be presented as working writes on
   them. FAT12/FAT32 layouts that share an existing failure/commit model belong
   in one cohesive migration slice. Independent format/provisioning or hardware
   persistence proofs remain separate. Reuse transport-neutral journal cores
   after inventory, not a second unsynchronized journal owner.
2. Add stable service-owned writable regular objects: owner/service/media/slot
   generations, explicit rights, no path reopen, no ambient delegation. Bind
   namespace and legacy mutators to the same object-lifetime exclusion, or
   reject unsupported mixed access before effects. Open-then-unlink, rename,
   inode/cluster reuse, media swap and service restart must not retarget a grant.
3. Admit the entire bounded write transaction before effects: validated object,
   exact input bytes, offset/length arithmetic, allocation/journal capacity,
   one owner and absolute deadline. Transfer in bounded bulk chunks without
   truncating long resources silently. A multi-chunk resource is not implicitly
   atomic: define durable progress/partial results explicitly. Growth, truncate,
   append and fixed-size writes sharing the same transaction contract should
   be implemented together, not split merely by fields or ABI variants.
4. Define ordered persistence and recovery: before-images/intent durable before
   target changes, data before metadata publication, validated readback,
   durable commit then cleanup. Lost reply != failed media write. Revoke/fence
   on ambiguity, no automatic replay or regrant. Old handles stay invalid after
   restart. Recovery reconciles evidence before read-write reintegration;
   budget exhaustion or conflicting evidence remains degraded/read-only.
5. Prove native faults/cuts at every persistence barrier, read/write/flush and
   recovery interruption, CRC/identity/sequence corruption and full-capacity
   boundaries. Real QEMU process fault/hang/cancel/restart, pinned-object stale
   calls, independent shell/volume liveness and bytewise old-or-final outcomes
   are required. Hardware power-loss claims need hardware evidence.
6. Only after the backend object contract passes, bind explicit CLI write grants
   and opaque JS methods through the existing broker. Browser and plain SCRIPT
   acquire no rights. GC never performs durable commit or OS cleanup; the host
   explicitly fences/reaps/closes. Preserve current JS execution/resource
   limits and distinguish normal errno from fatal protocol/outcome uncertainty.

Performance: no new synchronous RPC in GUI input, no per-byte I/O, no arbitrary
single-megabyte file limit; reuse bounded bulk transport/cache where qualified.
Queue/capacity limits protect failure containment, not an excuse for silent
short resources. Keep CPU-local, scheduler and framebuffer hotpaths untouched;
compare protected binaries and run existing benchmark proof if these paths
must change. JS4 process/admin rights remain later independent authority.
