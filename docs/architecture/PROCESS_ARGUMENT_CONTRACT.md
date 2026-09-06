# Bounded process arguments (R3.14)

Candidate, not accepted until host and guest gates pass. The 2026-09-06 user
approval extends the current browser repair to the existing spawn mechanism.

Reference: POSIX exec/spawn argv strings and E2BIG/EFAULT/ENOMEM error categories.
The existing REIST spawn syscall is an adapter, not a claim of POSIX execve
compatibility. Its number, calling convention and ownership do not change.
Sixteen public arguments and thirty-two internal loader arguments remain.
Each NUL-terminated string is at most 8193 bytes; all strings including NULs,
alignment and initial-stack argument metadata together use at most 16 KiB.
The existing guarded 32-KiB Ring-3 stack retains at least 16 KiB for execution.

Copy into one bounded kernel-owned packed snapshot before spawning. Invalid
pointers yield EFAULT, exhausted argument budgets E2BIG, failed allocation
ENOMEM. Cleanup is unconditional and no child is published on admission error.
Initial-stack construction independently validates the complete immutable
kernel snapshot before its first copyout. Copyout failure discards the
unpublished address space through the existing loader rollback.

No URL parser, protocol policy, new driver or syscall belongs in this change.
Host tests execute the actual admission and stack functions with faultable
copy/allocation backends. The controlled browser guest gate proves that long
CSS/image request targets survive the real spawn, HTTP and worker path.
