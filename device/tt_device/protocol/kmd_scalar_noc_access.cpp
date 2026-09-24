// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/protocol/kmd_scalar_noc_access.hpp"

#include <fmt/format.h>

#include <cstring>

#include "tt-kmd-lib/tt_kmd_lib.h"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

/**
 * The library's flag is the driver's flag under another name; translating rather than passing the
 * value through keeps the interface from inheriting the driver's numbering.
 */
uint32_t to_library_flags(uint32_t flags) {
    return (flags & ScalarNocAccess::FLAG_LOCAL_ADDRESS) != 0 ? TT_NOC_FLAG_KLA : 0;
}

}  // namespace

KmdScalarNocAccess::KmdScalarNocAccess(std::unique_ptr<PCIDevice> pci_device) : pci_device_(std::move(pci_device)) {
    UMD_ASSERT(pci_device_ != nullptr, error::RuntimeError, "Scalar access needs an open device.");

    handle_ = pci_device_->get_tt_device_handle();

    UMD_ASSERT(handle_ != nullptr, error::RuntimeError, "Scalar access needs an open driver handle.");
}

KmdScalarNocAccess::~KmdScalarNocAccess() = default;

void KmdScalarNocAccess::read(uint64_t addr, uint64_t* value, uint32_t width, uint32_t flags) {
    const int ret = tt_noc_read_scalar(handle_, addr, value, width, to_library_flags(flags));

    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format("Reading {} bytes at 0x{:x} failed: {}.", width, addr, std::strerror(-ret)));
}

void KmdScalarNocAccess::write(uint64_t addr, uint64_t value, uint32_t width, uint32_t flags) {
    const int ret = tt_noc_write_scalar(handle_, addr, value, width, to_library_flags(flags));

    UMD_ASSERT(
        ret == 0,
        error::RuntimeError,
        fmt::format("Writing {} bytes at 0x{:x} failed: {}.", width, addr, std::strerror(-ret)));
}

}  // namespace tt::umd
