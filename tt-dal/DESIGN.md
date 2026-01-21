# Design

This document captures the architectural philosophy and major design decisions
for the tt-dal C API.

## Principles

- **Mechanism**: This library provides raw hardware access without imposing
  policy decisions. Users control resource lifecycles, timing, and allocation
  strategies.
- **Stateless**: No global state registries or caches. Each handle contains
  exactly what's needed to interact with hardware. Thread safety comes from
  thread-local error storage, not locks.
- **Lightweight**: The kernel driver (tt-kmd) defines the contract. API
  structures mirror IOCTL structures directly. When in doubt, match kernel
  behavior, not existing UMD abstractions.
- **Transparent**: Handles are transparent structs users can inspect and
  stack-allocate. No opaque pointers requiring heap allocation and lifecycle
  management.
- **Foundational**: This is a foundation layer that higher-level code can build
  upon, not a convenience library.

### Non-Goals

What this library deliberately **does not** do:

- High-level abstractions (see [UMD])
- Resource pooling or caching
- Automatic resource management
- Convenience abstractions

[umd]: https://github.com/tenstorrent/tt-umd

## Decisions

### Architecture

#### Device Representation

Devices are represented as transparent structs containing a device ID and file
descriptor:

```c
typedef struct tt_device {
    uint32_t id;   // Device number (0, 1, 2, ...)
    int fd;        // File descriptor (-1 when closed)
} tt_device_t;
```

This design enables:

- **Stack allocation** without heap management.
- **Clear ownership** semantics (user owns the struct).
- **Cheap copying** of devices (shallow copies are valid).
- **Efficient reuse** of file descriptors across operations.

The alternative (opaque handles with internal registries) was rejected because
it introduces hidden state, requires allocation/deallocation APIs, and violates
the stateless principle.

> [!CAUTION]
>
> An issue with the current design using device numbers as an identifier is that
> post-reset, these identifiers may be reassigned to different physical devices.
>
> This will hopefully be addressed and have the implementation changed in a
> future update.

#### Resource Handles

Resource handles are transparent structs containing a kernel ID and associated
metadata. For example, TLBs:

```c
typedef struct tt_tlb {
    uint32_t id;           // kernel TLB ID
    void *ptr;             // allocation pointer (NULL until configured)
    size_t len;            // TLB window size
    uint64_t idx;          // Memory-map offset (for internal use).
} tt_tlb_t;
```

Resource handles are stack-allocated. Identifiers are designated per-device, so
two devices can independently use the same resource ID.

Users must carefully pair allocate/free calls and manage handle lifecycles. An
uninitialized or freed handle should not be reused.

**Benefit**: No hidden state, clear ownership, debuggable.

#### Architecture-Specific Code

Implementation for features that are architecture-specific live in separate
files (e.g. `src/arch/bh.c`) with a thin dispatch layer in `lib.c`. This makes
it trivial to add new architectures and keeps differences explicit and
localized.

### API Conventions

#### Error Handling Strategy

**TL;DR**: Functions return `-1` on error and set thread-local `tt_errno` to a
specific error code from `tt_error_t`.

The error handling follows **libc** conventions closely. All functions return
`-1` (defined as `TT_ERR`) to indicate failure and set the thread-local
`tt_errno` to a specific error code. On success, functions return `0` (defined
as `TT_OK`) or a meaningful non-negative value.

**Error setting pattern**:
```c
// Use comma operator to set errno and return -1
return tt_errno = TT_EINVAL, -1;
```

**Error checking pattern**:
```c
// Check for failure, then inspect tt_errno
if (tt_device_open(&dev) < 0) {
    const char *msg = tt_error_describe(tt_errno);
    fprintf(stderr, "Open failed: %s\n", msg);
}
```

### Safety & Correctness

#### Memory Management

**TL;DR**: Library never calls `malloc()`. All memory is caller-provided or
kernel-mapped.

No API function ever performs heap allocation. All memory is provided by the
caller or mapped from kernel resources.

**Key properties**:

- All handles are **caller-allocated**, typically on the stack. The library
  never allocates or frees these structures.
- **Zero internal state**: No global registries, no caches, no hidden lookup
  tables.
- Every operation is independent, using only the handles and parameters
  provided.
- Every syscall and side effect is explicit: no lazy initialization or
  background allocations.

**Benefits**:

- Users have **complete control** over memory.
- Can reason precisely about resource usage.
- Avoiding hidden allocations means **no hidden failure modes**.
- "What you see is what you get."

#### Device Lifecycle

Devices follow an **explicit open/close model** where all operations internally
ensure the device is open before use. Notably, operations will never implicitly
open a device.

```c
tt_device_t dev = { .id = 0, .fd = -1 };
if (tt_device_open(&dev) < 0) {
    // Handle error
}

// Use device for multiple operations
tt_device_get_info(&dev, &info);
tt_tlb_alloc(&dev, &tlb, size);

// Explicit cleanup
tt_device_close(&dev);
```

This gives users control over when to release resources while avoiding repeated
open/close overhead on every operation. Functions return `TT_ENOTOPEN` if called
on a unopened device.

#### TLB Lifecycle Safety

**TL;DR**: Deferred mmap prevents accessing unconfigured memory. Fail-fast with
`SIGSEGV`.

TLB windows follow a strict **allocate → configure → use → free** lifecycle
to prevent undefined behavior.

##### Memory Mapping

If `tt_tlb_alloc() mmap` immediately, the pointer would reference undefined
NOC address space until configuration. Users could accidentally read/write
through the pointer, causing silent data corruption or undefined hardware
behavior.

**Solution**: Defer `mmap` until `tt-tlb_configure`.

1. `tt_tlb_alloc()`: Allocates TLB ID in kernel, sets `ptr = NULL`.
2. `tt_tlb_configure()`: mmaps window, sets ptr, configures NOC mapping.
3. User accesses `tlb.ptr`: Valid pointer to configured device memory.
4. `tt_tlb_free()`: unmaps and clears the pointer to prevent further use.

If users attempt to use `ptr` before configuration, they get immediate `SIGSEGV`
(fail-fast) rather than silent corruption.

##### Reconfiguration

When reconfiguring an already-mapped TLB, the implementation mmaps a new address
first, then unmaps the old one. This ensures the kernel cannot reuse the old
virtual address, invalidating any stale interior pointers users may have saved.
Those pointers will fault on use rather than silently accessing different device
memory.

**Tradeoff**: Reconfigure incurs unmap/remap overhead (~microseconds), but
prioritizes **safety over performance**. Users building TLB pools can amortize
allocation cost; configuration is expected to be infrequent relative to actual
device access.

#### Reset Infallibility

**TL;DR**: Reset works even if device/driver is in a bad state. Never relies on
existing fd.

Device reset must work even when the device or driver is in a bad state. The
implementation never relies on the existing `fd` field of `tt_device_t`.

**Reset flow**:

1. Call `tt_device_close()` to close any existing fd (ignore errors).
2. Open dedicated _temporary_ fd directly.
3. Issue reset ioctl through temporary fd.
4. Close temporary fd.

This ensures reset works even if:

- The existing `fd` is corrupted or stale
- Previous operations left the fd in an undefined state
- The device was already in a bad state requiring reset

**Philosophy**: Reset is inherently messy (invalidates all fds and TLBs,
unavoidable races). Don't try to make it perfectly safe, just make sure the
reset operation itself can always execute. Users must accept that reset
invalidates all existing handles.

### API Design

#### Telemetry Snapshot API

**TL;DR**: Read entire telemetry table in one operation for consistency and
performance.

Telemetry is designed around **complete snapshots** rather than individual tag
queries. The API provides only `tt_get_telemetry()` which reads the entire
telemetry table in a single operation.

**Rationale**:

1. **Consistency**: Multiple reads of individual tags would produce mismatched
   data as device state changes between calls. A snapshot ensures all telemetry
   values represent the same moment in time.
2. **Performance**: Reading the entire table once is faster than repeated
   individual reads. The kernel fetches telemetry in bulk; exposing per-tag
   reads would encourage inefficient usage patterns.

Users who need only specific tags can read the full snapshot and ignore unused
values. The table is small enough that this is negligible overhead.

## Tradeoffs

These are deliberate design choices with considered tradeoffs:

### State Management

**Decision**: Stateless API with no global registries.

**Rejected**: Global registry tracking all devices and resources.

**Rationale**: Registries create hidden state that's hard to debug, require
locking for thread safety, and obscure lifecycle management. Stateless design
prioritizes simplicity and clarity at the cost of users tracking their own
metadata.

### Transparent Handles

**Decision**: Transparent structs handles with visible fields.

**Rejected**: Opaque handles with getter/setter APIs.

**Rationale**: Opaque handles require heap allocation, lifetime management APIs,
and hide state from debuggers. Transparent structs are C-idiomatic and directly
inspectable. Users may manipulate struct fields directly, which is acceptable
for a mechanism-only library.

### Memory Allocation

**Decision**: Stack-based API with caller-allocated handles. Library never calls
`malloc()` or performs dynamic allocation.

**Rejected**: Heap-allocated handles managed by library.

**Rationale**: Eliminating dynamic allocation removes entire classes of failure
modes (out-of-memory errors, memory leaks, use-after-free). Stack allocation
gives users complete control over memory layout and lifetime. Predictable memory
usage is critical for embedded systems and performance-sensitive code. The cost
is that users must manage handle storage themselves.

## Relationship to Other Layers

```
╭─────────────────────────────────────╮
│  tt-umd (C++ layer)                 │
│  - Policy, abstractions             │
│  - High-level API design            │
│  - Opinionated object model         │
╰─────────────────────────────────────╯
                 ↑
╭─────────────────────────────────────╮
│  tt-dal (this library)              │ ← You are here
│  - Stateless, transparent           │
│  - Mechanism-only API design        │
│  - Runs in userland                 │
╰─────────────────────────────────────╯
                 ↑
╭─────────────────────────────────────╮
│  tt-kmd (kernel driver)             │
│  - Low-level hardware interface     │
│  - IOCTLs as primary API            │
│  - Runs in kernel-space             │
╰─────────────────────────────────────╯
```

**Position**:

- **Above KMD**: Direct consumer of kernel IOCTLs. Functionality maps to kernel
  capabilities with minimal added functionality (e.g. telemetry).
- **Below UMD**: Foundation for higher-level libraries. UMD provides stateful,
  object-oriented abstractions.

## Future Extensibility

**Architectures**:

- Add new file in `src/arch/`, update dispatch in `lib.c`.
- See existing `arch/wh.c` and `arch/bh.c` for patterns.

**IOCTLs**:

- Update vendored IOCTL definitions.
- Add wrapper and follow existing patterns.

## Stability

Once stabilized (version 1.0.0), the library will maintain backwards
compatibility within major versions. Pre-1.0 releases have no compatibility
guarantees as the API is still evolving.

#### Breaking changes

The following changes are considered breaking changes, and require a major
version bump:

- Changing function signatures.
- Removing or renaming public API functions.
- Adding, removing, or reordering structure fields.
- Changing error code numeric values.
- Removing or changing enum values.

Note: Adding fields to transparent, caller-allocated structs breaks ABI because
it changes `sizeof()` and invalidates existing stack allocations.

#### Non-breaking changes

The following changes are permitted in minor or patch releases:

- Adding new functions.
- Adding new error codes with new numeric values.
- Adding new enum members.
- Adding new architecture support.
- Bug fixes that don't change API behavior.

These changes maintain backwards compatibility and do not require recompilation.

#### ABI compatibility

The library maintains ABI stability within major versions. Applications compiled
against version X.Y.Z will run with any version X.*.* without recompilation.

Binary compatibility is preserved through careful management of structure
layouts, function signatures, and symbol visibility.

### Deprecation

Features marked deprecated will remain functional for at least one major version
cycle. Deprecated APIs will be annotated with compiler warnings directing users
to replacements.
