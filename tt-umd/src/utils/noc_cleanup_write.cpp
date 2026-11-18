// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/utils/noc_cleanup_write.hpp"

#include <fcntl.h>  // O_APPEND
#include <fmt/format.h>

#include <cstring>
#include <tt-logger/tt-logger.hpp>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/kmd_versions.hpp"

namespace tt::umd {

NocCleanupWrite::NocCleanupWrite(int pci_device_num, uint8_t x, uint8_t y, uint8_t noc, uint64_t addr, uint32_t data) :
    device_path_(fmt::format("/dev/tenstorrent/{}", pci_device_num)) {
    const SemVer kmd_version = PCIDevice::read_kmd_version();
    UMD_ASSERT(
        kmd_version >= KMD_NOC_CLEANUP,
        error::RuntimeError,
        fmt::format(
            "NOC cleanup writes require KMD {} or newer, but this system runs KMD {}.",
            KMD_NOC_CLEANUP.str(),
            kmd_version.str()));
    // O_APPEND keeps this handle out of the device's power vote on KMD 2.6+; older KMD ignores it.
    int result = tt_device_open(device_path_.c_str(), &device_, O_APPEND);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_device_open() failed for NOC cleanup device {}: {}", device_path_, strerror(-result)));
    result = tt_device_set_noc_cleanup(device_, x, y, noc, addr, data);
    if (result != 0) {
        tt_device_close(device_);
        device_ = nullptr;
        UMD_THROW(
            error::RuntimeError,
            fmt::format("tt_device_set_noc_cleanup() failed on {}: {}", device_path_, strerror(-result)));
    }
}

uint64_t NocCleanupWrite::pin(void* addr, size_t size) {
    uint64_t dma_addr = 0;
    const int result = tt_pin_pages(device_, addr, size, TT_DMA_FLAG_NONE, &dma_addr, nullptr);
    UMD_ASSERT(
        result == 0,
        error::RuntimeError,
        fmt::format("tt_pin_pages() failed on NOC cleanup device {}: {}", device_path_, strerror(-result)));
    return dma_addr;
}

NocCleanupWrite::~NocCleanupWrite() noexcept {
    if (device_ == nullptr) {
        return;
    }
    const int result = tt_device_close(device_);
    if (result != 0) {
        log_warning(
            tt::LogUMD, "tt_device_close() failed for NOC cleanup device {}: {}", device_path_, strerror(-result));
    }
}

}  // namespace tt::umd
