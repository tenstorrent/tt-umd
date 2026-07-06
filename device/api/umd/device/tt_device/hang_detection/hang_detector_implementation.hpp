/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <optional>

#include "hang_detector.hpp"

namespace tt::umd {

class DeviceProtocol;
class PcieInterface;
enum class NocId : uint8_t;

/**
 * Reads a 32-bit NOC register at (core, addr) over the given NOC and returns its value.
 * Injection point for the HangDetector NOC liveness read; defaults to a DeviceProtocol-based
 * reader and can be overridden via set_noc_reg_reader().
 */
using NocRegReader = std::function<uint32_t(tt_xy_pair core, uint64_t addr, NocId noc)>;

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
class HangDetectorImplementation : public HangDetector {
public:
    // Public API. Returns std::nullopt when the underlying protocol
    // does not support the check.
    std::optional<bool> is_bus_hung(uint32_t data_read = HANG_READ_VALUE);
    std::optional<bool> is_noc_hung(NocId noc);

    // Overrides the NOC liveness register reader (see NocRegReader). An empty function is ignored,
    // so the default protocol-based reader stays in place.
    void set_noc_reg_reader(NocRegReader reader);

protected:
    HangDetectorImplementation(DeviceProtocol* protocol, architecture_implementation* arch_impl);

    DeviceProtocol* get_protocol() const { return protocol_; }

    PcieInterface* get_pcie_interface() const { return pcie_interface_; }

    architecture_implementation* get_arch_impl() const { return arch_impl_; }

    // Reads a 32-bit NOC register through the configured NocRegReader. Arch-specific variants compute the
    // (core, addr) for their hang-check register and route the actual read through here.
    uint32_t read_noc_reg(tt_xy_pair core, uint64_t addr, NocId noc);

    virtual bool is_bus_available() override { return pcie_interface_ != nullptr; }

    virtual bool is_noc_available() override { return is_mmio_protocol_; }

private:
    DeviceProtocol* protocol_;
    PcieInterface* pcie_interface_;
    bool is_mmio_protocol_;
    architecture_implementation* arch_impl_;
    NocRegReader noc_reg_reader_;
};

}  // namespace tt::umd
