// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

BlackholeArcApb::BlackholeArcApb(
    DeviceProtocol* device_protocol, PcieInterface* pcie_interface, JtagInterface* jtag_interface) :
    device_protocol_(device_protocol), pcie_interface_(pcie_interface), jtag_interface_(jtag_interface) {}

void BlackholeArcApb::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcApb::read is not implemented yet");
}

void BlackholeArcApb::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeArcApb::write is not implemented yet");
}

bool BlackholeArcApb::is_arc_available_over_axi() {
    UMD_THROW(error::RuntimeError, "BlackholeArcApb::is_arc_available_over_axi is not implemented yet");
}

int BlackholeArcApb::get_pcie_x_coordinate() {
    UMD_THROW(error::RuntimeError, "BlackholeArcApb::get_pcie_x_coordinate is not implemented yet");
}

}  // namespace tt::umd
