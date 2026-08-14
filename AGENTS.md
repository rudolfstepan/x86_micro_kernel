# REIST OS agent contract

## Mission

Build REIST OS as a bounded, fail-closed high-assurance research operating
system. Preserve stability, isolation, diagnosability and recoverability ahead
of throughput or feature count. Do not claim certification or fail-operational
behavior that has not been demonstrated on the target system.

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

1. Require a clean worktree. Stop if unrelated changes overlap allowed files.
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
6. Inspect the final diff for ABI drift, unbounded work, lost cleanup and stale
   documentation. For scheduler, memory, boot, IPC, persistence or privilege
   changes, use at most one read-only `reist_reviewer` subagent before commit.
7. On success, set the active package to `done`, set the next `queued` package
   to `active`, update `active_id`, and copy every targeted/package/runtime test
   command into `evidence` in its original order. When no queued package
   remains, set `active_id` to the empty string.
8. Commit the candidate with the exact `commit_message`, require a clean
   worktree and return the frozen gate list in `passed`. This is not accepted
   evidence until the outer verifier has actually passed every gate. Never push.
9. On ambiguity, missing required inputs or a pre-existing source failure: do
   not commit and return `blocked` with one concrete cause.

The outer runner is the only gate authority. It validates commit topology,
scope and queue transition before executing candidate code. It runs trusted
gate commands without a shell through `codex sandbox -P :workspace`, stops at
the first failure and fast-forwards the main branch only after all gates pass.

Do not use parallel writing agents. Subagents are optional and read-only;
their extra token cost must buy an independent, bounded audit.

## Non-negotiable engineering rules

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
  the clean reference build and complete host suite once.
- Runtime: the package-specific `test-reist-runtime.ps1` mode.
- Milestone: VMware reference package plus PIT, watchdog, recovery, memory and
  framebuffer smokes. Run only when the task file requests it.

Full logs belong under ignored `build/codex-agent/`. Report only command,
result, elapsed time and the last relevant lines of a failure.

## Communication

Use concise German. Lead with outcome, commit, passed gates and any remaining
risk. Do not repeat the roadmap or include routine tool transcripts.
