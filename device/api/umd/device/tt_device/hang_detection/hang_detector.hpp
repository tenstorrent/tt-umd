/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class DeviceProtocol;
class PcieInterface;
class TlbWindow;
enum class NocId : uint8_t;

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
    virtual ~HangDetector();

    // Public API. Returns std::nullopt when the underlying protocol
    // does not support the check.
    std::optional<bool> is_pcie_hung(uint32_t data_read = HANG_READ_VALUE);
    std::optional<bool> is_noc_hung(NocId noc);

protected:
    HangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl);

    DeviceProtocol* get_protocol() const { return protocol_; }

    PcieInterface* get_pcie_interface() const { return pcie_interface_; }

    architecture_implementation* get_arch_impl() const { return arch_impl_; }

    // Reads a single 32-bit NOC register for the hang check. On PCIe this goes through a dedicated,
    // un-timed TLB window guarded by its own lock — NOT the protocol's main cached window / io_lock_.
    // That keeps the probe safe to call from inside a timed-out memcpy (the intended on_timeout use):
    // it neither re-takes io_lock_ (no deadlock) nor re-enters the timed memcpy path (no recursion).
    // On non-PCIe protocols (e.g. JTAG) there is no timed path, so it falls back to a normal read.
    uint32_t read_noc_reg_via_probe_window(tt_xy_pair core, uint64_t addr, NocId noc);

private:
    // Arch-specific implementations.
    virtual uint32_t read_hang_check_reg_via_bar() = 0;
    virtual uint32_t read_hang_check_reg_via_noc(NocId noc) = 0;

    DeviceProtocol* protocol_;
    PcieInterface* pcie_interface_;
    bool is_mmio_protocol_;
    architecture_implementation* arch_impl_;

    // Dedicated NOC-probe window (PCIe only), created lazily on first probe and serialized by
    // probe_lock_. Separate from the protocol's cached window so probes never contend io_lock_.
    std::unique_ptr<TlbWindow> noc_probe_window_;
    std::mutex probe_lock_;
};

}  // namespace tt::umd
