/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/hang_detector.hpp"

#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

HangDetector::HangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl) :
    protocol_(protocol),
    pcie_interface_(dynamic_cast<PcieInterface*>(protocol)),
    is_mmio_protocol_(!dynamic_cast<RemoteInterface*>(protocol)),
    arch_impl_(arch_impl) {}

HangDetector::~HangDetector() = default;

uint32_t HangDetector::read_noc_reg_via_probe_window(tt_xy_pair core, uint64_t addr, NocId noc) {
    uint32_t value = 0;
    if (pcie_interface_ == nullptr) {
        // Non-PCIe protocol (e.g. JTAG): no timed memcpy path / io_lock_ to avoid, so read normally.
        protocol_->read_from_device(&value, core, addr, sizeof(value), noc);
        return value;
    }
    std::lock_guard<std::mutex> lock(probe_lock_);
    if (noc_probe_window_ == nullptr) {
        noc_probe_window_ = std::make_unique<SiliconTlbWindow>(
            pcie_interface_->get_pci_device()->allocate_tlb(arch_impl_->get_cached_tlb_size(), TlbMapping::UC));
    }
    try {
        noc_probe_window_->read_block_reconfigure(&value, core, addr, sizeof(value), noc);
    } catch (const error::DeviceTimeoutError&) {
        // The probe window is un-timed (no on_timeout), so an overrun throws rather than recursing.
        // If the liveness probe itself can't complete in budget, the NOC is unresponsive — report the
        // hang sentinel so is_noc_hung() returns true.
        return HANG_READ_VALUE;
    }
    return value;
}

std::optional<bool> HangDetector::is_pcie_hung(uint32_t data_read) {
    if (data_read != HANG_READ_VALUE) {
        return false;
    }
    if (!pcie_interface_) {
        return std::nullopt;
    }
    return read_hang_check_reg_via_bar() == HANG_READ_VALUE;
}

std::optional<bool> HangDetector::is_noc_hung(NocId noc) {
    if (!is_mmio_protocol_) {
        return std::nullopt;
    }
    NocIdSwitcher switcher(noc);
    auto result = read_hang_check_reg_via_noc(noc);
    return result == HANG_READ_VALUE;
}

}  // namespace tt::umd
