# Scheduler background execution, internal contract v1

Status: R1.1a accepted on 2026-09-07; all five host gates, both reference
builds and all five guest gates pass. Evidence: CURRENT_WORK.md, R1.1a.

The existing REIST class-window mechanism is not POSIX SCHED_FIFO/RR or
SCHED_DEADLINE. The established reference for runtime accounting and bandwidth
reclaiming is the [Linux deadline scheduler documentation](https://docs.kernel.org/scheduler/sched-deadline.html).
That EDF/CBS/GRUB model needs admission/deadline state that REIST does not have.
Do not copy its names or claim its guarantees. This append-only internal v1
extension instead permits only background execution when funded work is absent.

## Mechanism and limits

- Keep absolute per-CPU 100-ms windows and normal Safety/Service/Ambient limits
  of 60/25/15 ms. Keep the existing Safety,Safety,Service,Ambient cycle and
  per-class round robin. First select only compatible, owned-here/unowned-ready
  candidates whose class has normal budget.
- Only if this first pass finds nobody, select a compatible Ambient or Service
  background candidate with the same bounded cycle. Never borrow for Safety,
  after Safety depletion, on a clock fault, or from an uninitialized window.
- No reservation is transferred, replenished or debited to another class.
  Actual elapsed execution (including excess and nonpreemptible overshoot)
  remains charged to the executing class. Existing overload counters continue
  to mean reaching a normal limit, not an automatic new supervisor fault.
- Latch the selected effective class on a successful CPU claim. Subsequent IPC
  inheritance changes cannot retroactively reclassify that CPU's interval.
  Idle has no class. Existing task/generation/CPU ownership remains authoritative;
  the accounting latch grants no capability and cannot resurrect a task.
- A newly ready funded task excludes background work at the next existing
  scheduling opportunity. The periodic quantum stays 10 ms; existing bounded
  IRQ-lock deferral and nonpreemptible work still apply. This is not a new
  wake-to-run deadline guarantee. Late arrivals cannot be promised a whole
  reservation in the remaining fraction of a window, with or without reclaim.
- Clock faults remain latched, all classes denied, kernel recovery retained.
  Storage is fixed per CPU and per candidate. No new wait nodes, heap, I/O,
  formatted IRQ logs, locks, timer policies or public ABI.

## Proof boundary

Real policy and scheduler functions run in host tests at O0/O2, including
funded-priority exhaustion, all class/ready masks, real yield and claim paths,
inheritance changes, idle, affinity/CPU ownership, rollover and latched faults.
The existing packaged GTEST gains an explicit SCHED_SLACK diagnostic: bounded
1000-ms work, at most 4000000 clock samples, at least 400 observed consecutive
1-ms increments. This distinguishes the old roughly 15-percent application
ceiling from usable background execution; it is not process CPU-time accounting
or a WCET benchmark. Sleep/yield/kill/reuse and a fresh shell after a separate
Ring-3 exception remain required. APIC, PIT and two-vCPU guests have independent
180-second deadlines, 1024 MiB, snapshot disks and hidden windows.

The browser Surface candidate remains separately unaccepted in stash
`d9370608c5849bbae36663d515c7accd24930005`. Its 250/500-ms pixel-observed input
gates are not changed or accepted by this scheduler package.

The accepted QEMU image is preserved at
`build/codex-agent/r11a/accepted-scheduler.img`, SHA256
`ab1f5cdb55e1e12ee8834e66ee2b49e7bbd16fedb18e3c687a4222bbf27a25d3`.
APIC/PIT/two-vCPU workloads observe respectively 964/952, 861/870 and 980/966
adjacent ticks per 1000-ms phase. These are bounded guest workload observations,
not a utilization percentage, hardware qualification or browser latency result.
