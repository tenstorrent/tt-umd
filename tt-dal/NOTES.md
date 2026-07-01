# Pre-Stabilization Notes

This file tracks design decisions that need to be resolved before API
stabilization.

## Recent Decisions

### Explicit Device Open Required

**Decision:** Never auto-open devices. All API functions that need an
open device check `fd < 0` and return `TT_ENOTOPEN` if not opened.
Users must explicitly call `tt_device_open()` before using a device.

**Rationale:** More predictable, clearer control flow. Auto-open
behavior was considered "less explicit" and hides when syscalls happen.

**Status:** Implemented.

### Reset Infallibility

**Decision:** `tt_reset()` never relies on the `fd` field of
`tt_device_t`. It closes any existing fd, then opens and closes its
own dedicated fd for the reset operation.

**Rationale:** Reset must work even if existing fd is corrupted or in
bad state. Reset should be infallible.

**Status:** Implemented.

---

## Deferred Decisions

## Device Identification: BID vs Device Numbers

**Current:** `tt_device_t` uses `uint32_t id` for device numbers (e.g., 0, 1, 2)

**Problem:** Device numbers can change after reset, especially in multi-chip systems (8x4, 32-chip, 100+ chip configurations). Board IDs (BIDs) from `/dev/tenstorrent/by-id/` provide stable identifiers that persist across resets.

**Tradeoff:**
- **Device numbers (current):** Simple `uint32_t`, but unstable across resets in large systems
- **BIDs:** Stable identifiers (~49 chars), but requires carrying around potentially large strings

**Considerations:**
- BIDs require modern firmware + udev rules
- String handling adds complexity (comparison, storage, copying)
- Large chip counts (100+) make stability more valuable
- UMD hasn't integrated BIDs yet

**Decision:** Deferred. Need to evaluate whether stability benefit
outweighs string handling cost.

---

## tt_device_info Fields

**Current fields:**
```c
typedef struct tt_device_info {
    uint32_t output_size_bytes;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint16_t bus_dev_fn;
    uint16_t max_dma_buf_size_log2;
    uint16_t pci_domain;
} tt_device_info_t;
```

**Considerations:**
- Which fields are actually useful to library users?
- Are there missing fields that should be included?
- Should architecture be included here vs separate query?
- Is `output_size_bytes` needed for forward compatibility?
- Are all PCI fields necessary or too low-level?

**Decision:** Deferred. Need real-world usage to determine which
fields matter.

---

## Return Type Signedness

**Status:** RESOLVED in version 0.2.0

**Solution:** Separated error codes from return values.

**Changes Made:**
1. Renamed `tt_result_t` enum to `tt_error_t` (positive error codes only)
2. Moved `TT_OK` and `TT_ERR` out of enum, defined as macros
3. Changed all function return types to explicitly signed (`int` or `ssize_t`)
4. `tt_errno` type changed to `tt_error_t` (stores error codes, not return values)

**Result:**
- No enum signedness issues (all error codes are positive)
- Return values are explicitly signed (`int` is always signed in C)
- Clear separation between error codes (stored in `tt_errno`) and
  return values (0 or -1)
- Aligns perfectly with POSIX errno conventions

**Pattern:**
```c
int tt_device_open(tt_device_t *dev) {
    if (error_condition) {
        return tt_errno = TT_EINVAL, -1;  // tt_errno gets error code
    }
    return 0;  // Success
}
```

---

## Implementation TODOs

### Device Lost Handling (ENODEV Pattern)

When ioctl returns error with `errno == ENODEV`, the device has been
removed or become unavailable. Pattern to implement:

```c
ret = ioctl(dev->fd, ...);
if (ret < 0 && errno == ENODEV) {
    tt_device_close(dev);
    return -(tt_errno = TT_EDEVLOST);
}
```

**Locations:** All ioctl call sites in lib.c

**Status:** Not implemented yet.

### ARC Messaging Implementation

Implement architecture-specific ARC messaging in `wh.c` and `bh.c`.

**Wormhole:** Scratch registers (legacy, "disgusting")
**Blackhole:** Circular queues (modern, cleaner)

**Requirements:**
- Unified API hiding differences
- Blocking behavior (hide polling from users)
- Message struct is in-out (response overwrites request)

**Status:** Stubs only.

### Telemetry Implementation

Implement architecture-specific telemetry reading in `wh.c` and
`bh.c`.

**Design:** Snapshot API (complete table, not individual tags)

**Status:** Stubs only.

---
