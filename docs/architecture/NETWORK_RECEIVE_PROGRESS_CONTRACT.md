# Network receive progress, internal contract v1

Frozen 2026-09-08, R3.27a. User-approved prerequisite for the unaccepted
external-script candidate in stash `cb130c7b1b777e2f0e2c2afd7b42348b4c2713e2`.
No browser implementation or gate is accepted by this package.

## Cause and architecture

R3.27's real325231-byte HTTP source timed out within its unchanged5s budget.
The trace acknowledges103873 bytes in5.653 host seconds, with a maximum2048
receive window. The existing Ring-3 service drains8 frames, then waits40ms
for control IPC regardless of active traffic. The NIC bottom half already
runs outside IRQ; reopening ACKs after application reads already exist.

[RFC9293 section3.8.6](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.8.6)
defines the advertised receive window in bytes of available connection
storage, not the size of an application's individual copy operation. REIST
does not claim complete RFC9293 support. Window scaling, SWS refinement,
out-of-order reassembly and TCP state migration remain separate debt.

This is a correction to existing bounded storage/servicing, not permission
to add a new kernel protocol stack. Existing `tcp_socket.c` TCB state in
Ring0 is explicit migration debt. Parsers and traffic-sensitive waiting
remain Ring3. No NIC, DMA, scheduler, supervisor, IRQ or capability changes.

## Fixed limits and behavior

- Four static32KiB receive rings (128KiB total), no dynamic allocation.
  Public2048-byte receive copy and512-byte mediated ingress remain unchanged;
  kernel syscall stacks and public ABI sizes/numbers stay unchanged.
- Window advertises actual free storage, remains within16-bit unscaled TCP.
  Head/count arithmetic, rejection before mutation, ring wrap, sequence wrap,
  FIN ordering and existing immediate reopened-window ACK are preserved.
  Per-operation copy remains bounded by the old public capacities. Cleanup
  zeros storage and revokes the exact owner/generation; repeated cleanup and
  stale handles cannot affect a replacement socket.
- Ring3 keeps8-frame batches and control/health/DHCP work each turn. For100ms
  after successful RX use a finite1ms control wait, otherwise the original40ms.
  No polling with timeout0, no new wait node, allocation or global priority.
  Saturating activity deadline; failed/regressing clock never extends activity.
  Endpoint-loss path retains bounded sleep. No peer packet bypasses parsers.
- Existing timeouts, retries, service health/restart/fencing budgets and all
  browser deadlines remain fixed. Capacity pressure drops/retries rather
  than corrupting storage; this is not a no-loss or line-rate guarantee.

## Acceptance

Host O0/O2 executes real TCP and cadence code, not a model: 32KiB burst,
overflow, reopen ACK, ring/sequence wrap, byte integrity, EOF/reset/deadline,
all slots and cleanup/reuse, invalid owner/generation, cadence expiry/overflow.
Ring3 TCP parser checksum/length negative tests remain executable.

Packaged `nettest` is an explicitly diagnostic Ring3 command on the normal
shell search path in both image layouts. Its fixed local-peer protocol checks
every byte of1MiB in <=5000 guest milliseconds, slow-consumer recovery,
bounded stalled/reset connection, cancellation and fresh reuse. Test peer
binds only host loopback, uses finite connections/deadlines and no artificial
pacing of successful streams. Independent E1000 and RTL8139 QEMU guests;
retain bytes/times, serial logs and packet evidence. No public-network or
VMware runtime performance claim.

Both reference images must contain byte-identical accepted3eab01ab browser,
HTML worker, CURL, JSWORK and protected benchmark/test programs. Kernel hashes
are newly recorded. Existing scheduler-slack workload, browser input/native
fault/restart and real network service recovery remain mandatory. All logs
under `build/codex-agent/r327a-network/`, no overwritten old evidence.

R3.27 resumes only after this package passes. Its frozen5s source admission,
>256KiB HTTP script and all other gates remain mandatory; only the protected
kernel reference can then be explicitly rebased onto accepted R3.27a.
