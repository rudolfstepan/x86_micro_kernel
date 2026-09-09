# R3.39: Storage generation retirement before replacement

Frozen on08c8c364, 9 September2026; prerequisite inventory, not acceptance.
The standard lifecycle remains detect -> isolate -> revoke -> reap -> recreate
-> self-test -> reintegrate. This is an internal REIST supervisor mechanism,
not a POSIX signal/kill compatibility extension. Public syscall ABI unchanged.

## Failure and existing mechanisms

storage_service_poll and component_up discard process_terminate failures.
An AP-owned task is correctly refused by R3.38, but its supervisor can erase
the old identity and launch a replacement anyway. component_down checks the
return but does not quiesce or await reap. Spawn publishes an already runnable
process. Guard timeouts and manual recovery therefore need one retirement
protocol before additional persistent write authority is safe.

Reuse prepared supervised spawn, exact process identities, BSP affinity,
R3.38 atomic termination reservation, normal process cleanup/reap, protected
Storage control and the existing request-pool unbind. Do not add a scheduler
remote-kill mechanism, journal implementation, filesystem policy or JS rights.

## Frozen lifecycle

- Revoke healthy/bind authority in the protected control BEFORE unbind or
  termination. Retain PID/generation until process_identity_alive is false;
  its existing meaning includes terminating/cleanup until process-slot release.
- Add a generation-exact internal termination entry point; comparison and
  scheduler reservation share the Process -> Task critical section. Preserve
  process_terminate(pid), process_begin_exit and scheduler source unchanged.
- Request BSP affinity for the old generation, then attempt termination once
  per poll. Affinity change alone and successful termination are NOT reap proof.
  No sleep/spin loop in the periodic supervisor poll. Cleanup may use existing
  bounded sleepable mechanisms outside locks.
- Retirement has a1000ms monotonic deadline, capped by an earlier admin limit.
  One protected retirement field uses the former four-byte alignment gap;
  control remains64bytes, private version increments. Startup and retirement
  deadlines share the existing field in mutually exclusive states.
- On timeout, retain identity in an exhausted state, deny old bind/raw IO and
  replacement, emit one bounded diagnosis, apply existing write fences. Polls
  do not extend the deadline or consume more restart attempts. A new explicit
  admin recovery may retry retirement, never discard a surviving identity.
- Automatic timeout/fault, guard uncertainty, down/up and failed startup use
  the same retirement routine. A nonblocking lifecycle admission prevents two
  coordinating callers from racing across cleanup/sleep. Bind must remain
  callable during startup; it rejects retiring/exhausted identities.
- Prepared task identity is published before runnable admission. Publication
  or start failure never creates an untracked runnable Storage worker.
- Preserve resource quarantine/admin bits across slow calls and keep the
  desired post-ready AP mask across automatic replacements. Restore authority
  only through the existing fresh-generation self-test/bind.
- No heap, formatted diagnostics or I/O under a new spinlock. No extension of
  media write/flush authority or automatic replay of an uncertain transaction.

## Frozen proof and bounds

Queue contains12 exact groups. Regression first: compile actual extracted
control validation, lifecycle, bind/authorization and termination functions at
O0/O2 against explicit process/scheduler/IO boundary fakes. Refused termination,
late reap, stale/reused PID, AP return, expired deadline, exhaustion, manual
retry, failed publication/start, duplicate coordination and admission ordering
must fail closed. Test saturation and preserve existing fence/resource bits.
Compilers<=90s, native programs<=30s, Windows crash dialogs disabled.

Real QEMU1024MiB headless snapshot guests reuse the R3.38 private FAT12/EXT2
fixtures, fault/hang hooks, exact disk validation and shell commands unchanged.
Run normal/fault/hang on1CPU and fault/hang on4CPUs (aggregate<=450s,
each case<=180s). Require actual AP execution and retired PID/generation before
each replacement identity, fresh bind, retained uncertain-media denial and
unrelated file/shell progress. Real CPU-noncooperation exhaustion is not claimed
from the bounded native scheduling model. Never weaken old success markers.

Both reference builds, actual kernel/image agreement, all93 unchanged PRGs,
unchanged scheduler/CPU-local source, JS-file runner and external browser.
Archive logs/images/protected hashes; retain red/failed evidence. All gates
before done/next queue transition and local implementation commit. Never push.

## Backend inventory retained

FAT32 uses transport-neutral drivers/block/ata_journal.c v2 (20sectors) but
legacy ATA/AHCI ownership; Ring3 maintenance already knows its reserved layout.
FAT12 has a distinct64-entry journal plus remap/replicas, not the same protocol.
EXT2 already has Ring3 namespace journal v1, not a general data-write planner.
Future backend selection must explicitly address default FAT images and
exclusive journal ownership; EXT2-only proof cannot stand in for FAT writes.
