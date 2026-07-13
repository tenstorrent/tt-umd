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

namespace tt::umd {

/**
 * HangDetector checks whether the device hardware is hung.
 *
 * Depends only on DeviceProtocol for I/O. For BAR access it requires
 * a PcieInterface — returns std::nullopt if unavailable.
 * For NOC access it requires a non-remote protocol (PCIe or JTAG) —
 * returns std::nullopt for remote protocols.
 *
 * Arch-specific variants (Wormhole, Blackhole) override the protected
 * read_hang_check_reg_via_bar() and read_hang_check_reg_via_noc(NocId noc) with the
 * appropriate core type.
 */
class HangDetector {
public:
    virtual ~HangDetector() = default;

    // Public API. Returns std::nullopt when the underlying protocol
    // does not support the check.
    std::optional<bool> is_bus_hung(uint32_t data_read = HANG_READ_VALUE);
    std::optional<bool> is_noc_hung(NocId noc);

protected:
    // Arch-specific implementations.
    virtual uint32_t read_hang_check_reg_via_bar() = 0;
    virtual uint32_t read_hang_check_reg_via_noc(NocId noc) = 0;
    virtual bool is_bus_available() = 0;
    virtual bool is_noc_available() = 0;
};

}  // namespace tt::umd
