/*
 * SPDX-FileCopyrightText: (c) 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tt_device/hang_detection/hang_detector_implementation.hpp"

#include <utility>

#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/tt_device/protocol/remote_interface.hpp"
#include "tt-umd/types/noc_id.hpp"

namespace tt::umd {
HangDetectorImplementation::HangDetectorImplementation(DeviceProtocol* protocol, NocRegReader noc_reg_reader) :
    protocol_(protocol),
    pcie_interface_(dynamic_cast<PcieInterface*>(protocol)),
    is_mmio_protocol_(!dynamic_cast<RemoteInterface*>(protocol)),
    noc_reg_reader_(std::move(noc_reg_reader)) {
    if (!noc_reg_reader_) {
        noc_reg_reader_ = [this](tt_xy_pair core, uint64_t addr, NocId noc) -> uint32_t {
            uint32_t value = 0;
            protocol_->read_ctrl(&value, core, addr, sizeof(value), noc, RegisterAccessWidth::BITS32);
            return value;
        };
    }
}

uint32_t HangDetectorImplementation::read_noc_reg(tt_xy_pair core, uint64_t addr, NocId noc) {
    return noc_reg_reader_(core, addr, noc);
}
}  // namespace tt::umd
