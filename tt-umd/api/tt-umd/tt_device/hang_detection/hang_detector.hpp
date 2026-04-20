/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <optional>

#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/types/noc_id.hpp"

namespace tt::umd {

class DeviceProtocol;
class PcieInterface;

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
    std::optional<bool> is_pcie_hung(uint32_t data_read = HANG_READ_VALUE);
    std::optional<bool> is_noc_hung(NocId noc);

protected:
    HangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl);

    DeviceProtocol* get_protocol() const { return protocol_; }

    PcieInterface* get_pcie_interface() const { return pcie_interface_; }

    architecture_implementation* get_arch_impl() const { return arch_impl_; }

private:
    // Arch-specific implementations.
    virtual uint32_t read_hang_check_reg_via_bar() = 0;
    virtual uint32_t read_hang_check_reg_via_noc(NocId noc) = 0;

    DeviceProtocol* protocol_;
    PcieInterface* pcie_interface_;
    bool is_mmio_protocol_;
    architecture_implementation* arch_impl_;
};

}  // namespace tt::umd
