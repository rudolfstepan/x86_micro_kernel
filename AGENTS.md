# REIST OS agent contract

## Mission

Build REIST OS as a bounded, fail-closed high-assurance research operating
system. Preserve stability, isolation, diagnosability and recoverability ahead
of throughput or feature count. Do not claim certification or fail-operational
behavior that has not been demonstrated on the target system.

## Interactive execution directive

For this repository, the interactive Codex session performs implementation,
verification, queue transitions and local commits itself in the visible main
worktree. Do not invoke `run-reist-autonomous`, `codex exec`, subagents,
reviewer agents or any other nested agent/orchestrator unless the user
explicitly revokes this directive. This does not weaken package scope, frozen
gates, clean-worktree checks, bounded execution or the prohibition on pushing.

## Highest architecture rule

The microkernel is the protected failure-containment boundary. Ring 0 contains
only mechanisms required for scheduling, address spaces, IPC, capabilities,
interrupt entry, bounded device-resource mediation, fencing, watchdog and
supervisor transitions. Filesystems, protocol stacks, policy, complex parsers,
device drivers, system services, GUI components and applications belong in
separate Ring-3 processes. A crash, hang, invalid reply or quota violation in
one such process must be containable without kernel corruption or loss of
unrelated essential functions.

Every Ring-3 component has a generation-scoped lifecycle, fixed resource and
restart budgets, a health/self-test contract and explicit dependencies.
Recovery is `detect -> isolate -> fence/revoke -> reap -> recreate -> self-test
-> reintegrate`; exhaustion enters the profile-defined degraded or safe state.
Automatic restart and manual `svcctl` recovery use the same state machine and
never bypass fencing, generation checks or validation.

Do not add a new complex in-kernel driver as a shortcut. A hardware package
that lacks safe Ring-3 IRQ, MMIO/PIO and DMA mediation first adds that bounded
microkernel mechanism and its fault-injection proof. On hardware without an
IOMMU, never claim DMA fault isolation merely because driver code executes in
Ring 3; use a kernel-owned validated DMA mediator or declare the platform
unsupported for that assurance profile. Existing monolithic drivers are
visible migration debt, not architectural precedent.

## Sources of truth

Read only the material needed for the active package, in this order:

1. `automation/reist-s03b.toml` — executable queue and acceptance gates.
2. `docs/architecture/REIST_ARCHITECTURE.md` — target architecture.
3. `docs/development/OS_GAP_ANALYSIS_AND_ROADMAP.md` — order and DoD.
4. `docs/architecture/HIGH_ASSURANCE_CORE_CONTRACT.md` and the subsystem
   contract relevant to the package.
5. Current code and tests — always inventory existing mechanisms before adding
   another one.

The task file selects exactly one active package. Do not implement a later
package in the same run.

## Autonomous package protocol

1. Require a clean worktree before candidate implementation. An interactive
   coordinating agent may first inspect, stage and commit only package-contract,
   queue or documentation changes that it created for the user's current
   request. Stop if any unrelated or unattributed change is present, or if a
   user change overlaps package files.
2. Read the active package, its listed files and only the relevant doc sections.
3. Confirm the failure mode and add or tighten a regression test first when
   practical.
4. Make the smallest complete change within `allowed_files`. If another source
   file is required, stop and report the architectural reason; do not expand
   scope silently.
5. Do not run the listed acceptance gates inside the nested agent sandbox.
   Perform only bounded lightweight inspections that do not duplicate a gate.
   The outer runner executes every frozen gate exactly once in a separate
   `:workspace` verifier sandbox after validating the candidate commit.
6. Inspect the final diff directly for ABI drift, unbounded work, lost cleanup
   and stale documentation. Do not start subagents or reviewers in autonomous
   package runs; the outer gates provide the independent acceptance boundary.
7. On success, set the active package to `done`, set the next `queued` package
   to `active`, and update `active_id`. When no queued package remains, set
   `active_id` to the empty string. Leave gate evidence bookkeeping to the
   deterministic outer runner.
8. A nested package agent invoked by the outer runner must not stage or commit;
   it returns `candidate` with empty `commit`, `passed` and `blocker`. The
   interactive coordinating agent may stage and commit only files it created
   or edited for an explicit package-definition, queue, contract or
   documentation request, after inspecting `git status`, `git diff` and
   `git diff --check`. It must never include unrelated user changes. Candidate
   implementation commits remain owned by the outer runner, which validates
   scope, writes the frozen gate list, executes it and accepts it as evidence
   only after success. Never push.
9. On ambiguity, missing required inputs or a pre-existing source failure: do
   not commit and return `blocked` with one concrete cause.

The outer runner is the default candidate and gate authority. When the user
explicitly requests direct execution without a package agent, the interactive
agent may implement the active package in the visible main worktree, validate
the same frozen scope and queue transition, execute each frozen gate exactly
once, and commit only after all gates pass. It must not start another agent or
use this exception to skip an invariant, stop condition or gate. Never push.
Otherwise the interactive agent prepares and commits only contract/queue setup
and invokes the runner without asking for another routine handoff. The runner
validates commit topology, scope and queue transition before executing
candidate code. It runs trusted gate commands without a shell through
`codex sandbox -P :workspace`, stops at the first failure and fast-forwards the
main branch only after all gates pass.

Package agents operate in the visible main worktree so edits appear immediately
in the user's IDE. Do not create an isolated clone or Git worktree for package
implementation. The runner records the baseline commit, validates every changed
path before committing, and leaves in-place edits visible when a run is blocked.

Do not use subagents in autonomous package runs.

## Non-negotiable engineering rules

- Preserve the microkernel failure boundary above every feature goal. A
  service or driver feature is incomplete until its Ring-3 crash, hang and
  restart behavior is bounded and tested; normal component failure must not
  panic, corrupt or require rebooting the kernel.
- Design every public ABI, API, protocol, device abstraction, executable and
  persistent format standard-first. Name the applicable established reference
  standard and preserve its terminology, state model, units, error semantics
  and conventional toolchain integration wherever they fit the system. Do not
  claim source, binary or protocol compatibility until tested. A REIST-specific
  deviation is permitted only when isolation, bounded execution, diagnosability
  or recovery requires it; document the deviation, version it append-only and
  cover it with a regression test. Never silently reuse a standard name with
  incompatible semantics. Adopt mature observable contracts and best practices,
  not historical baggage such as unbounded waits, implicit global ownership,
  unchecked raw pointers, direct device authority or compatibility aliases that
  have no current use. Keep deliberate simplifications behind documented
  adapters so future source ports do not inherit obsolete implementation
  constraints.
- All waits, retries, queues and device operations are bounded by capacity or a
  monotonic deadline. No busy-wait in a userspace-facing safety path.
- Fail closed before side effects. Validate versions, sizes, generations,
  rights, ranges and user pointers before publishing state.
- Preserve public ABI compatibility: append syscall numbers, keep old wrappers,
  and version fixed-size structures.
- Safety-critical runtime paths use fixed-capacity storage. No heap allocation,
  blocking, VFS access or formatted logging in hard-IRQ/fatal paths.
- A task owns one intrusive wait node unless the scheduler contract explicitly
  changes. Never enqueue one node in two queues.
- Cleanup and revocation are idempotent and generation-scoped; stale handles
  must never regain authority.
- Kernel corruption is not repaired in place. Fence outputs, record bounded
  diagnostics and transition through the validated supervisor/watchdog path.
- Source-pattern tests supplement but never replace host behavior tests and a
  real QEMU guest proof for runtime claims.
- Preserve user changes and generated evidence; never use destructive Git
  recovery commands.

## Validation tiers

- Targeted: package-specific host/source tests.
- Package: `.\scripts\test-reist-package.ps1 -Target qemu -Video vga` runs
  the configured reference build and validates its required artifacts. The
  already frozen targeted tests are not repeated for every hardware/video
  variant. Pass `-RunHostTests` only for an explicit standalone full-host
  verification.
- Runtime: the package-specific `test-reist-runtime.ps1` mode.
- Milestone: the complete host suite once, VMware reference package plus PIT,
  watchdog, recovery, memory and framebuffer smokes. Run only when the task
  file requests it.

Full logs belong under ignored `build/codex-agent/`. Report only command,
result, elapsed time and the last relevant lines of a failure.

## Communication

Use concise German. Lead with outcome, commit, passed gates and any remaining
risk. Do not repeat the roadmap or include routine tool transcripts.

## Shell command integration

The normal interactive command interpreter is `/bin/shell.prg` in Ring 3; the
kernel command loop is only a rescue shell. Every new shell command must be
reachable from the userspace shell, either as a built-in or as a packaged
`.PRG` on its search path. Verify the userspace dispatch, both Windows and
Makefile image layouts, and a regression test for command resolution. A
kernel-rescue-shell entry alone does not satisfy the command requirement.
