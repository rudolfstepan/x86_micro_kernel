---
document_id: reist-cpp-migration-plan
version: "1.1"
status: approved
approved_on: "2026-09-06"
repository: "https://github.com/rudolfstepan/reist-os"
target_branch: working_branch
objective: "Selective C++ migration to reduce architectural complexity while preserving C ABI and low-level transparency."
---

# REIST OS: Selective C++ Migration Plan

Revision 1.1 incorporates the user-approved ownership, recovery, memory,
browser-priority and acceptance refinements. It changes planning requirements,
not accepted runtime behavior or frozen package gates. The original version 1.0
remains recorded by Git blob `1851b3571c2b26ec664bb792a841af23b9739f4c` in the
[committed baseline](development/CPP_MIGRATION_BASELINE.md).

The [executable queue](../automation/reist-s03b.toml) selects exactly one active
package and its allowed files and frozen gates. This plan does not authorize
later implementation outside that package. Current acceptance status:

| Work item | Status / evidence |
|---|---|
| TASK-0001 | Accepted in `8ff162a3`; baseline linked above |
| TASK-1001/1002 | Accepted in `478289b7`; [SDK profile 1](architecture/USERSPACE_SDK_AND_PORTABILITY.md) |
| TASK-2001 | Active as R3.17; regression-first work started, not accepted |
| TASK-3001 onward | Planned; no browser C++ migration acceptance |

## 1. Goals

- Preserve C syscall, IPC and public SDK ABI.
- Use C++ where ownership, lifecycle and state complexity justify it.
- Introduce RAII and stronger type-level invariants.
- Keep C and C++ interoperable indefinitely.
- Avoid hidden runtime behavior and uncontrolled allocation.

## 2. Non-goals

- No whole-OS migration.
- No mandatory C++ kernel.
- No C++ ABI across syscall, IPC or persistent-format boundaries.
- No exceptions or RTTI.
- No uncontrolled global constructors.
- No migration solely to replace functions with methods.

## 3. Language Policy

| Area | Policy | Priority |
|---|---|---|
| Boot/MMU/interrupt/context switch | C/ASM | preserve |
| Kernel scheduler/syscalls/low-level IPC | C | preserve |
| Hardware drivers | C by default | preserve |
| VFS/filesystems | C initially | preserve |
| Public SDK ABI | C | mandatory |
| IPC wire formats | C-compatible POD | mandatory |
| Browser | C++ | P0 pilot |
| GUI library | selective C++ | P1 |
| Desktop/compositor internals | selective C++ | P2 |
| Small CLI utilities | C by default | P3 |

Language choice does not determine privilege. Drivers, filesystems, protocol
stacks, GUI and applications belong in separate Ring-3 failure domains;
existing monolithic drivers are migration debt, not precedent for new Ring-0
code. The [target architecture](architecture/REIST_ARCHITECTURE.md) and
[core contract](architecture/HIGH_ASSURANCE_CORE_CONTRACT.md) remain binding.

## 4. Mandatory Architecture Rules

### CPP-RULE-001: C ABI boundaries

Syscalls, IPC wire structures, public SDK interfaces and persistent structures MUST remain C-compatible. C++ MAY implement logic behind those boundaries.

### CPP-RULE-002: Restricted C++ profile

Initial C++ compilation MUST use a freestanding-oriented profile equivalent to:

```text
-ffreestanding
-fno-exceptions
-fno-rtti
-fno-threadsafe-statics
-fno-use-cxa-atexit
```

### CPP-RULE-003: Explicit allocation

Foundational C++ abstractions MUST NOT perform hidden heap allocation. Allocation MUST be explicit or documented in the type contract.

Fixed-capacity containers are for bounded control data, not a requirement to
place whole documents, decoded images or caches inline. Large payloads use
explicit, fallible, process-private allocation with checked sizes and a declared
budget under the [existing memory contract](architecture/PRIVATE_PROCESS_MEMORY_CONTRACT.md).
Admission remains subject to available backing, reserves and per-process limits;
this plan neither enlarges those limits nor claims memory above the supported
address range. Future payload/cache packages MUST declare peak usage, reclaim
policy, quota failure behavior and navigation/exit cleanup. No eager allocation
of an entire budget and no implicit global collector or new kernel cache.

Borrowed views MUST document their owner and invalidation events, including
navigation, reset, reallocation and owner destruction. `Span` bounds alone do
not prove lifetime or grant authority; asynchronous work must validate the
existing owner generation before using the corresponding live storage.

### CPP-RULE-004: RAII

Owned resources SHOULD use deterministic RAII wrappers. Destructors MUST be `noexcept`.

`noexcept` alone is not a time bound. Destruction MUST have a documented bounded
release contract; it MUST NOT hide an unbounded wait, flush, retry or synchronous
recovery workflow. Fallible close/sync/stop/reap operations expose their outcome
through explicit APIs. A destructor may perform only the documented bounded
fallback; failed release must retain or transfer cleanup responsibility through
the existing lifecycle mechanism, never be reported as successful cleanup.

RAII covers normal object lifetime, not fault/kill cleanup. The OS retains
generation-scoped detection, isolation, fencing/revocation, reap, recreation,
self-test and reintegration with existing budgets. No claim that destructors
run after a crash, and no generic handle wrapper replaces this state machine.
Release traits consume existing authority only and define moved-from, reset,
release and failure behavior before integration.

Candidate resources:

- file descriptors
- sockets
- surfaces
- shared-memory mappings
- IPC endpoints
- process handles
- audio handles
- allocated buffers

### CPP-RULE-005: No C++ wire objects

The following MUST NOT cross IPC/syscall/public ABI boundaries:

- STL containers
- C++ classes
- virtual objects/vtables
- compiler-dependent layouts
- exceptions
- RTTI information

### CPP-RULE-006: Migration justification

A component MUST have at least one of these benefits before migration:

- ownership simplification
- lifecycle simplification
- invariant enforcement
- state encapsulation
- cleanup-path reduction
- reduced state plumbing
- stronger semantic typing

Fallible creation SHOULD use a named factory returning `Result<T, E>` (including
`Result<void, E>` for successful operations without a value). Success establishes
the published invariant; failure preserves the old state and error cause.
Explicitly closed or moved-from owners remain defined states, but a failed
constructor MUST NOT masquerade as successful resource acquisition.

## 5. Target Layering

```text
Ring 3: separate generation-scoped processes
  Browser / Desktop / GUI Apps (selective C++)
    -> optional libreist++ -> existing C SDK
  Parser / Transport / Filesystem / Driver services (C or selective C++)
    -> existing C SDK
                    |
           validated syscalls / IPC
================ protected failure boundary ================
Ring 0: Microkernel mechanisms (C / ASM)
  scheduling, address spaces, IPC/capabilities, interrupt entry,
  bounded device-resource mediation, fencing, watchdog/supervisor
```

This is the target boundary, not a claim that all existing drivers have already
been moved. Ring-3 placement alone does not prove DMA isolation without an
IOMMU or the validated kernel-owned DMA mediator required by the core contract.

## 6. Implementation Work Items

### TASK-0001: Establish baseline

**Priority:** P0  
**Dependencies:** none

Capture for browser, GUI library and compositor:

- source LOC
- init/destroy pairs
- cleanup/error branches
- ownership contracts/comments
- opaque state structs
- executable size
- relevant performance measurements
- current regression-test result

**Acceptance:**

- [x] Baseline committed.
- [x] Required baseline gates pass; evidence and limits are recorded.
- [x] Browser, GUI and compositor metrics recorded.

### TASK-1001: Enable mixed C/C++ toolchain

**Priority:** P0  
**Dependencies:** TASK-0001

Requirements:

- compile `.cpp` files;
- link C and C++ objects;
- preserve existing `.c` behavior;
- expose accidental runtime dependencies during build/link.

**Acceptance:**

- [x] Freestanding C++ test object builds.
- [x] Mixed C/C++ userspace executable links.
- [x] Existing C binaries remain buildable unchanged.
- [x] No hosted C++ runtime is implicitly required.

### TASK-1002: Define REIST C++ profile

**Priority:** P0  
**Dependencies:** TASK-1001

```yaml
cpp_profile:
  standard: C++20
  exceptions: false
  rtti: false
  global_dynamic_initialization: false
  hidden_heap_allocation: false
  c_abi_boundaries: true
  stl_default: forbidden
  stl_exception: explicit_architecture_review
```

### TASK-2001: Implement minimal `libreist++`

**Priority:** P0 (browser prerequisite)

**Dependencies:** TASK-1002

Initial types:

```text
Result<T,E>
Optional<T>
Span<T>
FixedString<N>
FixedVector<T,N>
UniqueHandle<T>
```

Requirements:

- no exceptions;
- no RTTI;
- no hidden allocation;
- deterministic destruction;
- move-only ownership where appropriate.

Keep the API limited to these six types and operations justified by the first
browser consumers. Use the `reist` namespace and document bounded deviations
from conventional C++ value/view/unique-owner semantics; no hosted-library
compatibility claim, STL reimplementation or speculative inheritance framework.
This allocation-free foundation does not impose inline storage on future large
payload owners. Existing R3.17 scope and gates remain unchanged.

**Acceptance:**

- [ ] Construction/destruction tests exist.
- [ ] Move semantics tested.
- [ ] No implicit heap/runtime dependency.
- [ ] Existing C API remains directly callable.

### TASK-3001: Browser response pilot

**Priority:** P0  
**Dependencies:** TASK-2001

Target: `browser_response`

Goals:

- a fallible factory publishes `Result<ValidatedResponse, ResponseError>` only
  after complete validation, preserving the existing C adapter/error contract;
- represent borrowed response storage and its lifetime explicitly; the current
  module owns no heap or handles and needs no invented cleanup/destructor;
- copy/move semantics explicitly defined;
- malformed input cannot expose partially initialized state.

Integrate the result into the real browser response-admission path; a parallel,
unused C++ implementation is not acceptance. Compare C/C++ behavior with the
same valid and malformed fixtures before switching the production caller.

### TASK-3002: Browser resource ownership

**Priority:** P0  
**Dependencies:** TASK-3001

Target: `browser_resources` and the corresponding real document/workspace owner,
only as explicitly listed in the future package contract.

Goals:

- ownership represented by types;
- lifetime tied to owning document/tab where applicable;
- explicit capacity/bounds policy;
- navigation cannot leave dangling resources.

The baseline module contains inline metadata and a caller-owned 1-MiB pool; it
does not own network/file handles or independently free heap storage. First
encapsulate validated snapshot publication, ranges and generations. Put RAII at
the actual acquisition/release sites in browser `main.c` and workers only when
those sites are included in the package's failure boundary and allowed files.
Do not combine independent transport, worker-recovery or cache-policy changes
merely to migrate their wrappers together. Large-payload growth is separately
budgeted under CPP-RULE-003, not a silent capacity change in this refactor.

### TASK-3003: Browser model

**Priority:** P0  
**Dependencies:** TASK-3002

Target: `browser_model`

The current model holds caller-owned layout, image-slot and scrollbar values;
navigation and child-process lifecycle live in browser `main.c`. Encapsulate
the existing value/range invariants without inventing a navigation-owning
`BrowserModel` or a polymorphic hierarchy. Fallible navigation/stop interfaces
belong to the audited actual owner and must preserve generation checks,
cancellation, bounded reap and last-valid-page publication. Any such owner
migration requires an explicit package scope; it is not implicitly granted by
this model task.

### TASK-3004: Evaluate remaining browser subsystems

**Priority:** P1  
**Dependencies:** TASK-3003

Candidates:

```text
html_engine
css_engine
html_document
browser_scene
browser_forms
browser_images
```

Parser hot paths MAY remain procedural even when compiled as C++.

Continue reusing pinned Hubbub/LibCSS and other mature C components. Wrap only
actual ownership boundaries; do not rewrite parsers as a side effect of changing
the host language or merge existing Ring-3 worker/transport processes.

**Browser pilot acceptance:**

- [ ] Functional behavior equivalent.
- [ ] Malformed/hostile input tests pass.
- [ ] No C++ ABI crosses public/process boundaries.
- [ ] Cleanup complexity measured before/after.
- [ ] Binary-size delta recorded.
- [ ] Performance delta recorded.
- [ ] Real production callers use the migrated path; no unused parallel model.
- [ ] Predeclared quantitative budgets pass (section 10).
- [ ] Applicable OOM, cancellation, stale-generation and fault/kill/reap checks pass.
- [ ] Architecture review confirms net complexity reduction.

**Stop condition:** Broad migration MUST stop if the pilot causes significant runtime/binary/debugging complexity without measurable architectural benefit.

### Browser delivery priority (cross-cutting)

TASK-2001 is followed immediately by the response/resources/model pilots in
dependency order, not more general library work. GUI/compositor migration is
not a browser prerequisite and remains deferred until browser-priority work
permits it. One cohesive package is implemented at a time; future contracts
must name the real production callers and their acceptance proof.

Language migration and browser compatibility are different deliverables.
Following the pilots, prioritize demonstrated browsing gaps under separate
feature contracts before broad GUI/compositor conversion. Each feature needs
deterministic HTTP/HTML fixtures and real guest interaction, an explicit
supported/unsupported matrix, error/recovery tests and resource budgets.
DOM/CSS/layout, forms, cookies/POST and JavaScript are not implicitly delivered
by a behavior-preserving C++ refactor and need not wait for every optional C++
candidate to migrate. Their dependency and authority boundaries remain in the
[browser engine plan](architecture/BROWSER_ENGINE_PORT_PLAN.md); a future
JavaScript engine stays behind the quota-, deadline- and generation-bounded
Ring-3 IPC/DOM boundary, not inside the compositor or kernel.

### TASK-4001: GUI value types

**Priority:** P1  
**Dependencies:** TASK-2001

Introduce strong/value types where useful, e.g.:

```cpp
struct Point { int x; int y; };
struct Size { int width; int height; };
struct Rect { Point origin; Size size; };
```

### TASK-4002: GUI state encapsulation

**Priority:** P1  
**Dependencies:** TASK-4001

Candidates:

```text
control
container
dialog
file_dialog
menu
tabs
text_editor
value_controls
font
font_catalog
```

Prefer concrete/final types. Do not introduce inheritance trees without a demonstrated polymorphism requirement.

### TASK-4003: GUI polymorphism review

**Priority:** P2  
**Dependencies:** TASK-4002

Evaluate explicitly:

- tagged unions;
- C-style function tables;
- templates/static polymorphism;
- virtual interfaces.

Virtual dispatch MUST NOT be the automatic default.

### TASK-5001: Compositor boundary audit

**Priority:** P1  
**Dependencies:** TASK-1002

Ensure these remain explicit C-compatible wire structures:

- client requests;
- surface descriptors;
- event messages;
- capability/handle identifiers.

### TASK-5002: Migrate compositor high-level state

**Priority:** P2  
**Dependencies:** TASK-5001, TASK-2001

Candidate conceptual components:

```text
WindowManager
SurfaceManager
Explorer
DragManager
TrashManager
FileTypeRegistry
ShortcutManager
```

Migrate only components showing lifecycle/ownership/state complexity. Do not automatically migrate rendering loops or IPC decoders.

## 7. Explicitly Deferred Areas

```yaml
deferred:
  - id: DEFER-001
    area: boot_mmu_interrupt_context_switch
    language: C_ASM
  - id: DEFER-002
    area: scheduler_syscall_dispatcher_low_level_ipc
    language: C
  - id: DEFER-003
    area: vfs_fat_ext2
    language: C
    reconsideration: separate_RFC_required
  - id: DEFER-004
    area: small_cli_utilities
    language: C
    reconsideration: only_if_internal_complexity_justifies
```

## 8. Coding Constraints

### Required

- explicit ownership;
- `noexcept` destructors;
- deleted copy operations for unique resources;
- explicit move semantics for ownership transfer;
- fixed-width integers at binary boundaries;
- `extern "C"` for C ABI interfaces;
- `static_assert` for binary layout assumptions where appropriate.

### Discouraged

- inheritance-heavy designs;
- shared ownership;
- implicit conversions;
- hidden allocation;
- global mutable state;
- runtime static initialization.

### Initially forbidden

```text
exceptions
RTTI
iostream
std::shared_ptr
unreviewed std::string
unreviewed std::vector
C++ objects in syscall/IPC ABI
compiler-dependent persistent serialization
```

## 9. C/C++ Interoperability Pattern

Stable C API:

```c
int x86os_open(const char* path, int flags);
int x86os_close(int fd);
```

Conceptual C++ wrapper (not a newly implemented SDK API):

```cpp
namespace reist {

enum class FileError;

class File final {
public:
    static Result<File, FileError> open(const char* path, int flags) noexcept;
    ~File() noexcept;

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    File(File&&) noexcept;
    File& operator=(File&&) noexcept;

    Result<void, FileError> close() noexcept;
    bool is_open() const noexcept;
    // Borrowed; no ownership transfer or lifetime extension.
    int native_handle() const noexcept;

private:
    explicit File(int owned_fd) noexcept;
    int fd_;
};

}
```

The wrapper MUST NOT alter the underlying C ABI.

`open` returns either the specific error or an owner of an acquired handle.
Closed/moved-from objects own nothing. The release adapter MUST define whether
each close error retains or consumes the handle and how cleanup remains owned;
do not blindly retry a raw descriptor that may have been reused. Durability
requires an explicit fallible sync operation under the existing filesystem
contract, not a destructor promise. Destruction is the bounded final fallback
specified by CPP-RULE-004; a required missing C lifecycle mechanism must be
addressed in its own authorized package before implementing this wrapper.

## 10. Verification Schema

Before implementation of each future migration package, freeze its fixtures,
source/build baseline, target/profile, sample count, measurement method and
numeric acceptance limits. Require binary-size and peak-private-memory limits;
for affected UI paths also input-to-paint latency and scroll/frame-time limits
(including the declared tail statistic), and for payload paths bytes copied and
allocation counts. Record units, absolute caps and allowed deltas. A metric
that genuinely does not apply needs a reason declared before implementation;
an applicable unset limit blocks that future package's start. Do not invent
thresholds from a different hardware profile or tune them after a failed gate.

Compare equivalent work and state on the same profile. The existing host
microbenchmarks are not UI latency or target WCET; lexical LOC/branch counts
are not semantic ownership or cleanup proofs. Record which invalid states or
manual ownership obligations were actually removed. Reuse accepted evidence
only for unchanged inputs. Run the frozen targeted, reference and guest gates
once per final candidate according to repository policy; do not repeat full
milestone suites for each helper. A failed gate follows the existing bounded
repair/stop protocol. This revision does not add, remove or relax frozen
R3.16/R3.17 gates or retroactively change their acceptance.

Every migrated subsystem MUST produce a review record compatible with:

```yaml
migration_review:
  component: ""
  measurement_contract:
    baseline_commit: ""
    fixture_digest: ""
    target_profile: ""
    method_and_sample_count: ""
    frozen_limits_with_units: {} # required applicable numeric caps/deltas
    not_applicable_with_reason: {}
  before:
    loc: null
    cleanup_paths: null
    init_destroy_pairs: null
    state_parameters: null
    binary_size_bytes: null
  after:
    loc: null
    cleanup_paths: null
    init_destroy_pairs: null
    state_parameters: null
    binary_size_bytes: null
  runtime:
    performance_delta_percent: null
    peak_private_bytes: null
    input_to_paint_ms: null
    scroll_frame_ms: null
    bytes_copied: null
    allocation_count: null
    all_applicable_limits_passed: null
  verification:
    regression_tests: null
    malformed_input_tests: null
    dependency_audit: null
    abi_review: null
    symbol_review: null
    production_callers: []
    ownership_and_invalid_state_delta: ""
    oom_cancel_stale_generation_tests: null
    guest_fault_kill_reap_tests: null
  conclusion:
    complexity_reduced: null
    keep_cpp: null
    notes: ""
```

## 11. Dependency Graph

These are technical dependencies, not permission to start independent GUI or
compositor branches ahead of browser delivery. Execution priority is governed
by the browser-delivery rule in section 6 and the sole active queue package.

```yaml
implementation_order:
  - { id: TASK-0001, depends_on: [] }
  - { id: TASK-1001, depends_on: [TASK-0001] }
  - { id: TASK-1002, depends_on: [TASK-1001] }
  - { id: TASK-2001, depends_on: [TASK-1002] }
  - { id: TASK-3001, depends_on: [TASK-2001] }
  - { id: TASK-3002, depends_on: [TASK-3001] }
  - { id: TASK-3003, depends_on: [TASK-3002] }
  - { id: TASK-3004, depends_on: [TASK-3003] }
  - { id: TASK-4001, depends_on: [TASK-2001] }
  - { id: TASK-4002, depends_on: [TASK-4001] }
  - { id: TASK-4003, depends_on: [TASK-4002] }
  - { id: TASK-5001, depends_on: [TASK-1002] }
  - { id: TASK-5002, depends_on: [TASK-5001, TASK-2001] }
```

## 12. Definition of Done

The selective C++ initiative is successful when:

- [ ] C++ is supported without requiring a hosted C++ runtime.
- [ ] Existing C ABI remains compatible.
- [ ] Browser pilot demonstrates measurable structural simplification.
- [ ] RAII removes meaningful manual cleanup complexity.
- [ ] No new implicit allocation or exception dependency exists.
- [ ] GUI migration proceeds only where pilot evidence supports it.
- [ ] Kernel and low-level architecture remain independently buildable and understandable as C/ASM components.
- [ ] Each migration has before/after metrics and an explicit keep/revert decision.
- [ ] Quantitative budgets and applicable lifecycle failure tests pass.
- [ ] Browser feature acceptance is tracked separately from language migration.

## 13. Agent Instruction

When implementing this plan, an automated coding agent MUST:

1. process tasks in dependency order;
2. inspect current repository state before modifying a component;
3. preserve externally observable behavior unless a task explicitly changes it;
4. avoid opportunistic unrelated refactoring;
5. keep each migration reviewable in small commits;
6. run relevant tests after every component migration;
7. record deviations from this plan;
8. stop and request architectural review if satisfying a task requires violating a mandatory rule;
9. prefer reverting an unsuccessful C++ migration over retaining complexity merely because migration work has already been performed.
