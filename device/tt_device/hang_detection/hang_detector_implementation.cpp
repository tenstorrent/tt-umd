/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/hang_detection/hang_detector_implementation.hpp"

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/types/noc_id.hpp"

namespace tt::umd {
HangDetectorImplementation::HangDetectorImplementation(
    DeviceProtocol* protocol, architecture_implementation* arch_impl) :
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

void HangDetectorImplementation::set_noc_reg_reader(NocRegReader reader) {
    if (reader) {
        noc_reg_reader_ = std::move(reader);
    }
}

uint32_t HangDetectorImplementation::read_noc_reg(tt_xy_pair core, uint64_t addr, NocId noc) {
    return noc_reg_reader_(core, addr, noc);
}
}  // namespace tt::umd
