// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

// tt-kmd-lib device handle. Forward declared so this public header does not require tt-kmd-lib's
// include path, which UMD links privately.
struct tt_device_t;

namespace tt::umd {

// A NOC write KMD performs when this object's device handle closes: when the object is destroyed, or
// when the process dies without destroying it. Lets device-side software learn that its host client
// is gone, even after a crash.
//
// Owns a dedicated device handle, opened with O_APPEND so it does not request high power. The handle
// is close-on-exec, but a child forked without exec shares it, and then KMD writes only once the child
// has closed it too. Not copyable or movable.
class NocCleanupWrite {
public:
    // Registers a write of `data` to `addr` on tile (x, y) of NOC `noc` (0 or 1) of
    // /dev/tenstorrent/<pci_device_num>. `addr` must be 4-byte aligned. Throws when the device cannot
    // be opened or KMD rejects the action (it needs KMD 2.2.0+).
    NocCleanupWrite(int pci_device_num, uint8_t x, uint8_t y, uint8_t noc, uint64_t addr, uint32_t data);
    ~NocCleanupWrite() noexcept;

    NocCleanupWrite(const NocCleanupWrite&) = delete;
    NocCleanupWrite& operator=(const NocCleanupWrite&) = delete;
    NocCleanupWrite(NocCleanupWrite&&) = delete;
    NocCleanupWrite& operator=(NocCleanupWrite&&) = delete;

private:
    std::string device_path_;
    tt_device_t* device_ = nullptr;
};

}  // namespace tt::umd
