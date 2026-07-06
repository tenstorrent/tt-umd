/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/hang_detector.hpp"

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

HangDetector::HangDetector(DeviceProtocol* protocol, architecture_implementation* arch_impl) :
    protocol_(protocol),
    pcie_interface_(dynamic_cast<PcieInterface*>(protocol)),
    is_mmio_protocol_(!dynamic_cast<RemoteInterface*>(protocol)),
    arch_impl_(arch_impl),
    // Default reader: plain protocol read. A higher layer may override it via set_noc_reg_reader().
    noc_reg_reader_([this](tt_xy_pair core, uint64_t addr, NocId noc) -> uint32_t {
        uint32_t value = 0;
        protocol_->read_from_device(&value, core, addr, sizeof(value), noc);
        return value;
    }) {}

void HangDetector::set_noc_reg_reader(NocRegReader reader) {
    if (reader) {
        noc_reg_reader_ = std::move(reader);
    }
}

uint32_t HangDetector::read_noc_reg(tt_xy_pair core, uint64_t addr, NocId noc) {
    return noc_reg_reader_(core, addr, noc);
}

std::optional<bool> HangDetector::is_bus_hung(uint32_t data_read) {
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
