# Pinned musl numerical sources

Origin: https://musl.libc.org/releases/musl-1.2.6.tar.gz
Release: 1.2.6, 20 March 2026, verified download on 7 September 2026.
SHA256: `d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a`.

Only the exact unmodified binary64 algorithms/support files selected by
`scripts/build_user_math.py` are compiled, not the musl Linux libc or allocator.
The original archive preserves complete provenance, COPYRIGHT and per-file
notices (MIT and compatible Sun/BSD/Arm notices). The SDK installs COPYRIGHT
and every selected source/header under `usr/share/licenses/musl-math`.
REIST's opt-in public headers and thread-local x87/MXCSR fenv adapter are
separate source files, not silent patches to upstream algorithms. Scope and
deliberate API limits: `docs/architecture/RING3_MATH_RUNTIME_CONTRACT.md`.

The original i386 `sqrtl.c` supplies only generic acosh's extended x87
intermediate evaluation, renamed to a hidden internal helper. No public
long-double family is exported. The build adapter disables upstream's
obsolete drem weak alias; numerical source bytes remain unchanged.
