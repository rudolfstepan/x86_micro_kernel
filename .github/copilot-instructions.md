# REIST OS coding instructions

Follow the repository-root `AGENTS.md`. The active autonomous package is in
`.codex/tasks/reist-s03b.toml`; implement only that package and satisfy every
listed invariant and test gate.

Architecture and status are authoritative only in:

- `docs/architecture/REIST_ARCHITECTURE.md`
- `docs/architecture/HIGH_ASSURANCE_CORE_CONTRACT.md`
- `docs/development/OS_GAP_ANALYSIS_AND_ROADMAP.md`

Inspect current code before changing it. Do not rely on historical GRUB,
fixed-address userspace, monolithic stack or old syscall-table descriptions.
