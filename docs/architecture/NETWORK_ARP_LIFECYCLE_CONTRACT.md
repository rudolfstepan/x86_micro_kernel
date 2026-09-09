# R3.41a: Ring-3 ARP request lifecycle

User-approved independent prerequisite, 9 September2026. Clean source baseline
e44215a9 (runtime7d87119c). The unaccepted R3.41 candidate is preserved in stash
c8cd59ea26548e97ad1a7e9ef69a9a4d4421f22a and under
build/codex-agent/r341-journal-handoff/. Its16/17 gates are not acceptance;
neither its source changes nor its candidate images belong to this package.

## Failure and authority boundary

The existing kernel retires an ARP probe after250ms; the Ring-3 network service
retains pending_network_probe_id until a reply arrives. A legitimate later
NETA request then takes return16. Actual extracted service and kernel authority
code reproduce request1/success -> expiry -> request2/exit16 at O0/O2. A late
binding denial or an ordinary transmit failure can similarly take return13/17.
The browser failure's second ARP request/service replacement is consistent with
this defect, but its exact service exit code was not captured in that run.

Preserve the existing RFC826 Ethernet/IPv4 ARP wire protocol and REIST-specific
single-use, generation-bound admission. No new public ABI, kernel parser,
supervisor/driver policy, timeout, queue, privilege, command or persistent format.
The existing kernel network implementation remains migration debt. Ring3 must
not confuse an expired request with process corruption. Only the current kernel
authority may authorize transmission or binding; local correlation is not a
grant. Existing malformed-input/integrity rejection and recovery budgets remain.

## Frozen implementation

1. Validate the complete existing26-byte NETA envelope and all its fields before
   attempting mediation. A pending local probe alone is not a fatal error.
   Let the existing kernel syscall validate the exact current request and
   generation. Replace local probe correlation only after successful admission;
   retire an older console correlation then, without publishing stale replies.
2. Ordinary EAGAIN/EACCES/EIO/ETIMEDOUT from ARP request/binding mediation are
   bounded refused operations, not reasons to exit the service. Do not retry
   the consumed capability or fall back to raw transmit. Unknown/integrity
   errors retain fail-closed service termination. Stale/duplicate/mismatched
   messages must not clear a newer probe or install an unvalidated binding.
3. Keep late response checks before publication. Retire only the matching
   local pending correlation on refused binding. Preserve the existing
   network-degradation reporting, receive batches, control/health cadence,
   DHCP and TCP behavior. No new allocation, busy-wait or timer state.
4. Actual O0/O2 native code covers expired predecessor/fresh successor, stale
   and duplicate messages, malformed fields, denied transmit/binding, fatal
   integrity results, direct/console correlation and successful later progress.
   Use actual production branches/helpers, not a rewritten behavioral model.
5. Real headless1024MiB QEMU on both E1000 and RTL8139: the loopback Ethernet
   peer withholds ARP responses across multiple kernel deadlines, then supplies
   a valid current response and ICMP reply. Require observed retries, successful
   PING, unchanged network service generation, independent root/shell activity,
   and fresh post-failure work. No private production fault command is needed.
   Peer framing, queues and logs are bounded; retain failed and successful
   transcripts, packet/event evidence and exact image hash. Each NIC <=90s;
   aggregate <=180s. Native compile <=90s/run <=10s, no host error dialogs.
6. Both reference builds and93 actual packaged programs: only REIST.PRG may
   change from the accepted R3.40 payloads; both kernels and all other payloads
   must remain byte-identical. No benchmark/throughput claim. Existing TCP
   progress/cancellation guests, network fault/recovery and external-script
   browser gates must pass without budget increases or assertion relaxation.

## Completion and return to R3.41

Freeze13 groups in the queue before edits. Inspect source/scope and archive
accepted artifacts, then commit only this independent package and return the
queue to R3.41. Do not implement R3.41 in this run. Its stash is retained, not
popped or deleted. On resumption restore its owned changes with explicit
CURRENT_WORK reconciliation, preserve the new accepted network service and
rebase its artifact guard only onto this package's accepted REIST.PRG hash.
R3.41's original17 gates/limits remain; affected final image/browser evidence
must be renewed before its implementation commit. R3.6b stays deferred.
