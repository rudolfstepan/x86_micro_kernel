---
document_id: reist-cpp-migration-plan
version: "1.0"
status: proposed
repository: "https://github.com/rudolfstepan/reist-os"
target_branch: working_branch
objective: "Selective C++ migration to reduce architectural complexity while preserving C ABI and low-level transparency."
---

# REIST OS: Selective C++ Migration Plan

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

### CPP-RULE-004: RAII

Owned resources SHOULD use deterministic RAII wrappers. Destructors MUST be `noexcept`.

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

## 5. Target Layering

```text
Applications: Browser / Desktop / GUI Apps
                 C++
                  |
          libreist++ wrappers
     RAII / Result / Span / Fixed containers
                  |
        Existing REIST C SDK ABI
                  |
             Syscalls / IPC
                  |
          Kernel / Drivers
              C + ASM
```

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

- [ ] Baseline committed.
- [ ] Existing tests pass before migration.
- [ ] Browser, GUI and compositor metrics recorded.

### TASK-1001: Enable mixed C/C++ toolchain

**Priority:** P0  
**Dependencies:** TASK-0001

Requirements:

- compile `.cpp` files;
- link C and C++ objects;
- preserve existing `.c` behavior;
- expose accidental runtime dependencies during build/link.

**Acceptance:**

- [ ] Freestanding C++ test object builds.
- [ ] Mixed C/C++ userspace executable links.
- [ ] Existing C binaries remain buildable unchanged.
- [ ] No hosted C++ runtime is implicitly required.

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

**Priority:** P1  
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

- constructor establishes valid state;
- destructor owns cleanup;
- copy/move semantics explicitly defined;
- malformed input cannot expose partially initialized state.

### TASK-3002: Browser resource ownership

**Priority:** P0  
**Dependencies:** TASK-3001

Target: `browser_resources`

Goals:

- ownership represented by types;
- lifetime tied to owning document/tab where applicable;
- explicit capacity/bounds policy;
- navigation cannot leave dangling resources.

### TASK-3003: Browser model

**Priority:** P0  
**Dependencies:** TASK-3002

Target: `browser_model`

Suggested conceptual interface:

```cpp
class BrowserModel final {
public:
    Result<void, BrowserError> navigate(...);
    void stop() noexcept;

private:
    // explicitly owned state
};
```

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

**Browser pilot acceptance:**

- [ ] Functional behavior equivalent.
- [ ] Malformed/hostile input tests pass.
- [ ] No C++ ABI crosses public/process boundaries.
- [ ] Cleanup complexity measured before/after.
- [ ] Binary-size delta recorded.
- [ ] Performance delta recorded.
- [ ] Architecture review confirms net complexity reduction.

**Stop condition:** Broad migration MUST stop if the pilot causes significant runtime/binary/debugging complexity without measurable architectural benefit.

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

Optional C++ wrapper:

```cpp
namespace reist {

class File final {
public:
    explicit File(const char* path, int flags) noexcept;
    ~File() noexcept;

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    File(File&&) noexcept;
    File& operator=(File&&) noexcept;

    bool valid() const noexcept;
    int native_handle() const noexcept;

private:
    int fd_;
};

}
```

The wrapper MUST NOT alter the underlying C ABI.

## 10. Verification Schema

Every migrated subsystem MUST produce a review record compatible with:

```yaml
migration_review:
  component: ""
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
  verification:
    regression_tests: null
    malformed_input_tests: null
    dependency_audit: null
    abi_review: null
    symbol_review: null
  conclusion:
    complexity_reduced: null
    keep_cpp: null
    notes: ""
```

## 11. Dependency Graph

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
