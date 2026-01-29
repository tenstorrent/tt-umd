# Design

This document captures the architectural philosophy and major design decisions
for the tt-dal C API.

## Principles

**Mechanism, Not Policy**: This library provides raw hardware access without
imposing policy decisions. Users control resource lifecycles, timing, and
allocation strategies.

**Stateless Architecture**: No global state registries or caches. Each handle
contains exactly what's needed to interact with hardware. Thread safety comes
from thread-local error storage, not locks.

**KMD as Source of Truth**: The kernel driver (tt-kmd) defines the contract.
API structures mirror IOCTL structures directly. When in doubt, match kernel
behavior, not existing UMD abstractions.

**Transparency Over Abstraction**: Handles are transparent structs users can
inspect and stack-allocate. No opaque pointers requiring heap allocation and
lifecycle management.

**Pure C Interface**: This is a foundation layer that higher-level code can
build upon, not a convenience library.

## Policy

### Device Representation

Devices are represented as transparent structs containing a device ID and file
descriptor. This allows:
- Stack allocation without heap management
- Direct inspection in debuggers
- Clear ownership semantics (user owns the struct)
- Efficient reuse of file descriptors across operations

The alternative (opaque handles with internal registries) was rejected because
it introduces hidden state, requires allocation/deallocation APIs, and violates
the stateless principle.

### Error Handling Strategy

Errors use a dual-value system: functions return negative error codes (POSIX
convention), while thread-local `tt_errno` stores positive values suitable for
process exit codes. This supports both programmatic error handling and
command-line tool development.

Error codes are limited to 255 due to exit code constraints and organized by
categories (general, device, transport, hardware) rather than proliferating
hundreds of specific codes.

### Device Lifecycle

Devices follow an explicit open/close model where all operations internally
ensure the device is open before use. This gives users control over when to
release resources while avoiding repeated open/close overhead on every
operation.

The API functions calling `tt_device_open()` internally means users never see
errors from unopened devices - the library handles it transparently. This
trades a small amount of magic for significant ergonomic improvement.

### Resource Handles

TLB handles and similar resources are transparent structs containing the kernel
ID and associated metadata (pointer, size). Like device handles, these are
stack-allocatable and directly inspectable. The kernel ID space is per-device,
so two devices can independently use the same TLB ID.

Users must carefully pair allocate/free calls and manage handle lifecycles. No
sentinel values - an uninitialized or freed handle should not be reused. The
benefit: no hidden state, clear ownership, debuggable.

### Architecture-Specific Code

Wormhole and Blackhole have different implementations for ARC messaging and
telemetry. Rather than ifdef soup in a single file or runtime function pointer
dispatch, architecture-specific code lives in separate files with a thin
dispatch layer.

This makes it trivial to add new architectures and keeps differences explicit
and localized.

### Telemetry Snapshot API

Telemetry is designed around complete snapshots rather than
individual tag queries. The API provides only `tt_get_telemetry()`
which reads the entire telemetry table in a single operation.

**Rationale for snapshot-only design:**

1. **Consistency**: Multiple reads of individual tags would produce
   mismatched data as device state changes between calls. A snapshot
   ensures all telemetry values represent the same moment in time.

2. **Performance**: Reading the entire table once is faster than N
   individual reads. The kernel fetches telemetry in bulk; exposing
   per-tag reads would encourage inefficient usage patterns.

3. **Discourages anti-patterns**: Not providing `get_telemetry_tag()`
   prevents users from repeatedly querying individual tags in loops,
   which would be both slow and produce inconsistent results.

Users who need only specific tags can read the full snapshot and
ignore unused values. The table is small enough (~70 uint32_t values)
that this is negligible overhead.

### TLB Lifecycle Safety

TLB windows follow a strict allocation → configuration → use
lifecycle to prevent undefined behavior.

**Problem:** If `tt_tlb_alloc()` mmapped immediately, the pointer
would reference undefined NOC address space until configuration. Users
could accidentally read/write through the pointer, causing silent data
corruption or undefined hardware behavior.

**Solution:** Deferred mmap until first configuration.

1. `tt_tlb_alloc()` - Allocates TLB ID in kernel, sets `ptr = NULL`
2. `tt_tlb_configure()` - mmaps window, sets ptr, configures NOC
   mapping
3. User accesses `tlb.ptr` - Valid pointer to configured device memory

If users attempt to use `ptr` before configuration, they get immediate
`SIGSEGV` (fail-fast) rather than silent corruption.

**Reconfiguration safety:** When reconfiguring an already-mapped TLB,
the implementation mmaps a new address first, then unmaps the old one.
This ensures the kernel cannot reuse the old virtual address,
invalidating any stale interior pointers users may have saved. Those
pointers will fault on use rather than silently accessing different
device memory.

**Tradeoff:** Reconfigure incurs unmap/remap overhead (~microseconds),
but prioritizes safety over performance. Users building TLB pools can
amortize allocation cost; configuration is expected to be infrequent
relative to actual device access.

### Reset Infallibility

Device reset must work even when the device or driver is in a bad
state. The implementation never relies on the existing `fd` field of
`tt_device_t`.

**Reset flow:**

1. Call `tt_device_close()` to close any existing fd (ignore errors)
2. Open dedicated temporary fd directly
3. Issue reset ioctl through temporary fd
4. Close temporary fd

This ensures reset works even if:
- The existing `fd` is corrupted or stale
- Previous operations left the fd in an undefined state
- The device was already in a bad state requiring reset

**Philosophy:** Reset is inherently messy (invalidates all fds and
TLBs, unavoidable races). Don't try to make it perfectly safe - just
make sure the reset operation itself can always execute. Users must
accept that reset invalidates all existing handles.

### Direct IOCTL Mapping

Where possible, API structures are 1:1 with kernel IOCTL structures. We don't
"clean up" or abstract the kernel interface. If the kernel returns PCI
subsystem vendor ID, we expose it. If the kernel uses PCI device IDs as
architecture identifiers, our enum uses those values.

This makes the library a thin, predictable wrapper. Users who understand the
kernel driver can predict the API behavior.

## Tradeoffs

### Stateless vs Registry

**Choice**: Stateless (no global device/TLB registries)

**Rationale**: Registries create hidden state that's hard to debug, require
locking for thread safety, and make lifecycle management opaque. The cost of
stateless (users track metadata) is outweighed by simplicity and clarity.

### Transparent vs Opaque Handles

**Choice**: Transparent structs with visible fields

**Rationale**: Opaque handles require heap allocation, lifetime management
APIs, and hide state from debuggers. Transparent structs are more C-idiomatic
and give users full visibility.

**Risk**: Users might manipulate struct fields directly. Acceptable because
the library is mechanism-only - advanced users who do this know what they're
doing.

### Inline Helpers vs Library Functions

**Choice**: Simple getters (version, enum-to-string) are static inline in the
header

**Rationale**: Zero runtime cost, no .c file needed for trivial code, reduces
library size. Standard practice for simple constant returns and lookups.

### Error Granularity

**Choice**: ~40-50 error codes organized by category, not hundreds of specific
errors

**Rationale**: Exit codes limited to 8 bits. Most errors at this level are
"device not found" or "out of memory" - specifics come from kernel errno or
logs, not API error codes. Over-specific error codes create maintenance burden.

### Power Management

**Choice**: Library signals power-aware mode to kernel, users manage power
explicitly via IOCTL

**Rationale**: Power management is policy. This library is mechanism. Kernel
provides the power control interface; this library makes it accessible without
imposing a power management strategy.

## Relationship to Other Layers

**Above tt-kmd**: Direct consumer of kernel IOCTLs. Maps 1:1 with kernel
capabilities.

**Below UMD**: Foundation for higher-level libraries. UMD can build stateful,
object-oriented abstractions on top of this stateless C API.

**Peer to tt_kmd_lib**: Replaces the existing C wrapper by being more minimal,
more direct, and built from scratch with clearer design principles.

## Future Extensibility

New architectures: Add new file in `arch/`, update dispatch in `lib.c`.

New IOCTLs: Add wrapper in appropriate file, follow existing patterns.

New error codes: Add within existing categories, preserve numbering gaps.

Backwards compatibility: Structure fields can be added to the end, functions
can be added, but existing signatures are frozen once released.
