/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <optional>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class DeviceProtocol;

/**
 * HangDetector checks whether the device hardware is hung.
 *
 * Depends only on DeviceProtocol for I/O. For BAR access it dynamic_casts
 * the protocol to PcieInterface — returns std::nullopt if the cast fails.
 *
 * Arch-specific variants (Wormhole, Blackhole) override the protected
 * read_hang_check_reg_via_bar() and read_hang_check_reg_via_noc() with the
 * appropriate core type.
 */
class HangDetector {
public:
    virtual ~HangDetector() = default;

    // Public API. Returns std::nullopt when the underlying protocol
    // does not support the check.
    std::optional<bool> is_pcie_hung(uint32_t data_read = HANG_READ_VALUE);
    std::optional<bool> is_noc_hung(NocId noc);

protected:
    HangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl, tt_xy_pair hang_check_core);

    // Arch-specific implementations.
    virtual uint32_t read_hang_check_reg_via_bar() = 0;
    virtual uint32_t read_hang_check_reg_via_noc() = 0;

    DeviceProtocol* get_protocol() const { return protocol_; }

    architecture_implementation* get_arch_impl() const { return arch_impl_; }

    tt_xy_pair get_hang_check_core() const { return hang_check_core_; }

private:
    DeviceProtocol* protocol_;
    architecture_implementation* arch_impl_;
    tt_xy_pair hang_check_core_;
};

}  // namespace tt::umd
