# MMIO memcpy timeout

**Status:** Draft
**Date:** 2026-05-13
**Author:** pjanevski (with Claude)

## Problem

`memcpy_to_device` and `memcpy_from_device` (`device/pcie/device_memcpy.{hpp,cpp}`)
issue AVX2 / SSE / 4-byte / byte-wide loads and stores against TLB-mapped device
memory. When the NOC is hung, these transactions can stall the host indefinitely.
The existing SIGBUS guard (`SiliconTlbWindow::execute_safe`,
`device/pcie/silicon_tlb_window.cpp:239`) recovers when the PCIe controller surfaces
a completion timeout as a SIGBUS, but two failure modes are not covered:

1. **Slow degradation** — transactions still complete, just one or two orders of
   magnitude slower than normal. No SIGBUS is raised; the host grinds.
2. **Posted-write pile-up** — writes are posted and complete at the CPU before
   they reach silicon. A bulk block transfer can issue hundreds of kilobytes of
   stores into a dead device before back-pressure stalls the CPU and SIGBUS
   eventually fires. We want to stop issuing transactions earlier.

## Non-goals

- **Preempting a single stalled instruction.** When the CPU is stalled in an
  MMIO load/store, no userspace code runs until the transaction returns or
  SIGBUS is delivered. A software timer cannot break out of that stall; the
  PCIe completion timeout and the existing SIGBUS path remain the only recovery
  from a single hung transaction.
- **Replacing the SIGBUS guard.** This change adds a software budget on top of
  the existing SIGBUS-based protection. SIGBUS remains the primary mechanism.
- **Covering single-word access paths.** `write32` / `read32` / `write16` /
  `read16` / `write_register` / `read_register` issue a single (or
  word-at-a-time) MMIO transaction whose bound is the PCIe completion timeout.
  Adding a `steady_clock::now()` call around each 4-byte store would cost more
  than it saves.

## Design

### Low-level: deadline-aware `memcpy_to_device` / `memcpy_from_device`

Add an overload that accepts an absolute deadline:

```cpp
// device/pcie/device_memcpy.hpp
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size,
                      std::chrono::steady_clock::time_point deadline);

void memcpy_from_device(void* dest, const volatile void* src, std::size_t size,
                        std::chrono::steady_clock::time_point deadline);
```

Existing no-deadline overloads remain and delegate with
`std::chrono::steady_clock::time_point::max()` (i.e., effectively unbounded —
preserves today's behavior for any caller we don't migrate).

Inside the deadline overload:

- Sample `steady_clock::now()` once at function entry.
- In **Phase 1** (the unbounded 256-byte AVX2 loop), check `now() >= deadline`
  once per iteration, *after* the 8 stores/loads complete. On overrun, throw
  `error::DeviceTimeoutError` with the elapsed time and the number of bytes
  remaining.
- Phases 0, 2, 3, 4, 5 process at most `< 256 B` combined — bounded tail work,
  no check needed.

Rationale for per-iteration granularity: a `clock_gettime`/`rdtsc` is ~10–30 ns;
each 256 B iteration is hundreds of ns of memory work. One sample per iteration
is < 5 % overhead even on a healthy system and yields ~µs-scale detection
latency once an instruction does return.

### Mid-level: `SiliconTlbWindow::write_block` / `read_block`

Both paths compute a deadline at entry and pass it down:

```cpp
auto deadline = std::chrono::steady_clock::now() + get_mmio_timeout();
```

The WH-specific private `SiliconTlbWindow::memcpy_to_device` and
`memcpy_from_device` (the RMW-tolerant wrappers at `silicon_tlb_window.cpp:137`
and `:176`) gain a `deadline` parameter and forward it to the low-level free
functions for their middle bulk segment. The leading/trailing RMW words are
single transactions; if they hang, SIGBUS catches them.

### Configuration surface

Mirror the existing `set_sigbus_safe_handler` pattern with a static setter on
`SiliconTlbWindow`:

```cpp
// device/api/umd/device/pcie/silicon_tlb_window.hpp
class SiliconTlbWindow : public TlbWindow {
public:
    // ...
    static void set_mmio_timeout_ms(uint32_t ms);
    static std::chrono::milliseconds get_mmio_timeout();
};
```

Backing storage is a function-local `static std::atomic<uint32_t>` inside
`get_mmio_timeout()`, initialized once via `std::call_once` from the env var
`TT_UMD_MMIO_TIMEOUT_MS`. If the env var is unset or unparseable, default
**1000 ms**. `set_mmio_timeout_ms` overwrites the atomic regardless of whether
the env-var initialization has run, and triggers initialization if it hasn't.

Why a static rather than per-window: a NOC hang is a device-wide condition, and
the same budget should apply to every window into that device. A per-cluster or
per-device setter is a reasonable follow-up if we discover different windows
need different budgets, but YAGNI for now.

### New error type

```cpp
// device/api/umd/device/utils/error.hpp
class DeviceTimeoutError : public std::runtime_error {
public:
    explicit DeviceTimeoutError(const std::string& message)
        : std::runtime_error(message) {}
};
```

Distinct from `SigbusError` so callers can tell the difference (slow
degradation vs hardware fault) and retry policies can diverge if needed.

`DeviceTimeoutError` is thrown from inside `memcpy_to_device` /
`memcpy_from_device`. The `execute_safe` template at
`silicon_tlb_window.cpp:239` does not need changes — it already propagates
exceptions from `func(...)` to the caller. The existing SIGBUS catch-and-rethrow
remains for the (still primary) SIGBUS path.

### Worst case and ordering

The deadline check sits between Phase 1 iterations, never inside an iteration.
If the final iteration before the check stalls in an MMIO transaction, total
wall time is bounded by:

```
total wait ≤ budget + (PCIe completion timeout for one in-flight transaction)
```

Document this in the header so callers know the timeout is best-effort, not a
strict upper bound. The check between iterations is safe with respect to the
existing volatile-store ordering invariant in `device_memcpy.cpp:27-29` —
Phase 1 iterations are already sequentially ordered with respect to each
other; inserting a `steady_clock::now()` comparison between them does not
introduce any new reordering window.

## Testing

### Unit tests (no device)

In `tests/unified/test_tlb.cpp` or a new `tests/unified/test_device_memcpy.cpp`:

1. **Already-expired deadline throws.** Call
   `memcpy_to_device(dst, src, 4096, steady_clock::now() - 1ms)` to a host
   buffer. Expect `DeviceTimeoutError`. (Destination need not be device memory
   for this — the deadline check fires before any silicon access.)
2. **Generous deadline completes.** Same call with `now() + 1s`. Expect normal
   completion and byte-for-byte equality with `std::memcpy`.
3. **Tiny size (≤ 256 B) bypasses the bulk loop.** Same as (2) for sizes that
   exercise only Phases 2–5; ensure no false-positive throw.
4. **Env var default.** Save/clear/set `TT_UMD_MMIO_TIMEOUT_MS`, call
   `SiliconTlbWindow::get_mmio_timeout`, assert the read-back value. Run after
   `set_mmio_timeout_ms` is called to confirm runtime override wins.

### Hardware-in-the-loop

Cannot reliably reproduce a NOC hang from a unit test. We rely on the existing
SIGBUS-based long-jump tests (`tests/baremetal/test_long_jump.cpp`) for the
hardware path and add a smoke test that confirms a healthy device's bulk
write/read completes well under the default 1 s budget.

## Files touched

- `device/api/umd/device/utils/error.hpp` — add `DeviceTimeoutError`.
- `device/pcie/device_memcpy.hpp` — add deadline overloads, document semantics.
- `device/pcie/device_memcpy.cpp` — implement deadline overloads; existing
  overloads forward with `time_point::max()`.
- `device/api/umd/device/pcie/silicon_tlb_window.hpp` — add
  `set_mmio_timeout_ms` / `get_mmio_timeout`; private member functions gain
  `deadline` parameter.
- `device/pcie/silicon_tlb_window.cpp` — wire up deadlines in
  `write_block` / `read_block` and the private memcpy helpers; lazy env-var
  initialization.
- `tests/unified/test_device_memcpy.cpp` (new) — unit tests above.
- `nanobind/py_api_tt_device.cpp` — expose `DeviceTimeoutError` to Python the
  same way `SigbusError` is exposed.

## Out of scope (follow-ups)

- Watchdog thread + sticky "device is dead" flag (Approach B in the
  pre-design discussion). Useful for blocking *future* transactions after a
  hang; tracked separately if retry storms appear in practice.
- Extending the budget to single-word paths.
- Per-window or per-cluster timeout configuration.
