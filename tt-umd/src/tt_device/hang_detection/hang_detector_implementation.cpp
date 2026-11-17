// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/hang_detection/hang_detector_implementation.hpp"

#include <utility>

#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

HangDetectorImplementation::HangDetectorImplementation(DeviceProtocol* device_protocol, PcieInterface* pcie_interface) :
    device_protocol_(device_protocol), pcie_interface_(pcie_interface) {}

void HangDetectorImplementation::set_noc_probe_window(std::unique_ptr<IoWindow> window) {
    std::lock_guard<std::mutex> lock(noc_probe_window_mutex_);
    noc_probe_window_ = std::move(window);
}

bool HangDetectorImplementation::is_bus_available() { return pcie_interface_ != nullptr; }

bool HangDetectorImplementation::is_noc_available() { return device_protocol_ != nullptr; }

uint32_t HangDetectorImplementation::read_noc_register(tt_xy_pair core, uint64_t addr, NocId noc) {
    {
        std::lock_guard<std::mutex> lock(noc_probe_window_mutex_);
        if (noc_probe_window_) {
            noc_probe_window_->configure(TargetIoWindowConfig{core, std::nullopt, addr, noc});
            return noc_probe_window_->read32(0);
        }
    }
    uint32_t value = 0;
    device_protocol_->read_ctrl(&value, core, addr, sizeof(value), noc, RegisterAccessWidth::BITS32);
    return value;
}

}  // namespace tt::umd
