// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/utils/noc_cleanup_write.hpp"

#include <fcntl.h>  // O_APPEND
#include <fmt/format.h>

#include <tt-logger/tt-logger.hpp>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

NocCleanupWrite::NocCleanupWrite(int pci_device_num, uint8_t x, uint8_t y, uint8_t noc, uint64_t addr, uint32_t data) :
    device_path_(fmt::format("/dev/tenstorrent/{}", pci_device_num)) {
    // O_APPEND tells KMD (>= 2.6) not to request high power on open; the flag is harmless on older KMD versions.
    int result = tt_device_open(device_path_.c_str(), &device_, O_APPEND);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_open() failed for NOC cleanup device {} errno: {}", device_path_, -result));
    result = tt_device_set_noc_cleanup(device_, x, y, noc, addr, data);
    if (result != 0) {
        tt_device_close(device_);
        device_ = nullptr;
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "tt_device_set_noc_cleanup() failed on {} errno: {}; it needs KMD 2.2.0+", device_path_, -result));
    }
}

NocCleanupWrite::~NocCleanupWrite() noexcept {
    if (device_ == nullptr) {
        return;
    }
    const int result = tt_device_close(device_);
    if (result != 0) {
        log_warning(tt::LogUMD, "tt_device_close() failed for NOC cleanup device {} errno: {}", device_path_, -result);
    }
}

}  // namespace tt::umd
