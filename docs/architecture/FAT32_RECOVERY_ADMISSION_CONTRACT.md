# R3.40: complete FAT32 journal recovery admission

Frozen 2026-09-09 on clean c1130a39. Active package scope and ten gates are
in automation/reist-s03b.toml. This repairs an existing persistence defect;
it does not implement the subsequent Ring-3 writable-object backend.

## Failure and boundary

The transport-neutral drivers/block/ata_journal.c currently validates and
restores entries one at a time. A late bad CRC/read/range can therefore reject
a mount after earlier target sectors were changed. Recovery also admits the
mirror header as an undo target, unlike normal writes; v1 admits every journal
sector. Duplicate targets can restore contradictory before-images. Invalid
volume geometry can read outside the declared volume; a small v1 reservation
can be upgraded into a v2 journal whose twenty data slots do not fit.
Finally, rejected reattach of a pending transaction disables its live owner.

The present kernel adapter serializes the core and fences failed recovery.
That legacy filesystem/journal ownership is migration debt, not permission
for new kernel filesystem policy. Repair this already shared portable core
before reusing it in Ring 3. Do not create a second owner through raw-sector
syscalls: those currently pass through the legacy journal and object guard.

## Contract

- Preserve the existing REIST-specific RSTJ v1 reader and v2 512-byte record,
  state/sequence selection, twenty-slot bound and ATA transport contracts.
  These are not a FAT-standard transaction extension or a compatibility claim.
  Guest media follow the existing FAT32 BPB layout and FAT32 cluster-count
  classification (at least65525 data clusters), not a tiny FAT12-like fixture.
- Reject reattach with pending entries in a live transaction before changing
  any journal state or invoking transport. A clean/empty inherited transaction
  retains the existing behavior.
- Validate nonzero volume, nonoverflowing exclusive end and reserved extent
  before reading. Small valid unmarked/nonjournal reservations remain allowed
  but unattached; marked v1/v2 need the full current v2 undo extent before
  recovery or upgrade. Unreadable unmarked headers are not proof of absence.
- Validate every selected undo target inside the volume, outside header/undo
  slots and the present mirror sector. Reject duplicate v2 targets. Validate
  all undo reads and CRCs into the existing instance-owned undo_data before
  the first target/header write. No heap or large stack buffer. At most20
  targets and190 pair comparisons; no new wait/retry loop.
- Malformed geometry/headers/targets, failed undo reads and bad CRCs produce
  no media writes or flushes. The caller must retain its existing mount/IO
  fencing after attach returns false. The core is not an authority broker.
- After admission, retain reverse target restore followed by CLEAN headers.
  Each synchronous transport write retains its existing persistence contract.
  A write failure/cut may partially restore old data, but never erase the undo
  evidence before all targets are restored. Retry after each write cut must
  converge to exact old data and clean headers; another attach is idempotent.
- Keep normal transaction begin/end, batching, read and write code identical
  to c1130a39. No scheduler/CPU-local/GUI change; no new performance claim.

## Proof and limits

First run native regression against the old actual core, with dialogs disabled
and fixed timeouts. Native O0/O2 covers v1/v2, single/mirror/max-capacity,
geometry, CRC/read/range/duplicate/self-target, header contradiction, owner
preservation and every recovery write interruption plus retry. Existing actual
FAT32 write/truncate/directory-extension whole-image cut campaign still passes.

Private headless QEMU auxiliary FAT32 images cover valid v1/v2 and late CRC,
mirror and duplicate targets. Verify entire media byte-for-byte, repeated
legacy mount refusal, independent root reads and normal userspace shell after
Storage restart. Reference system image is snapshot-only and unchanged.
The separate Ring-3 read parser does NOT yet acquire this journal admission
boundary; do not claim its reads on failed legacy mounts are rejected.
Both reference builds/new kernels, all93 unchanged payloads and existing JS
file/browser guests complete acceptance. No physical power-loss guarantee.

## Following backend slice (not implemented here)

Default FAT32 is the first backend; FAT12 has an independent64-entry journal,
remapping and replicas and cannot silently use this20-entry protocol. Inventory
the existing FAT32 allocator/directory/files/VFS code for Ring-3 reuse and define
exclusive journal handoff, raw mediation and cache invalidation before moving
ownership. Then add service-owned writable objects and whole bounded transaction
admission (write/grow/truncate/append together), explicit partial progress and
lost-reply fencing. Only afterward delegate explicit JS write capabilities.

## Accepted evidence, 2026-09-09

Contract checkpoint b37f6c36; all ten frozen groups pass. Evidence and complete
command/time map are in CURRENT_WORK and build/codex-agent/r340-fat32-recovery.
Native O0/O2 each pass103 case groups (including every recovery write cut,
retry and idempotence), existing complete FAT32 image fault campaign passes.
Both final builds/new kernels/all93 unchanged payloads and unchanged normal
journal/scheduler hotpaths verified. Five real1024MiB QEMU cases/25 commands pass
in92.321s; three rejected whole disks stay byte-identical across repeated mount
and Storage restart, while independent root reads and userspace shell survive.
Existing JS-file and browser guests pass.144 hash-verified reference files kept.
Initial native failures and later nine-reserved-sector boundary failure remain.
Two earlier guest failures were new fixture/parser errors (hdd1 mount naming,
complete supervisor-generation restart record), corrected with native negatives;
requirements and deadlines unchanged. No new authority or physical durability
claim; the separate Ring-3 read parser still needs journal ownership/admission.
