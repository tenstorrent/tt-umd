// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"

#include <cstddef>
#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

BlackholeArcApb::BlackholeArcApb(
    DeviceProtocol* device_protocol, PcieInterface* pcie_interface, JtagInterface* jtag_interface) :
    device_protocol_(device_protocol), pcie_interface_(pcie_interface), jtag_interface_(jtag_interface) {}

void BlackholeArcApb::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

void BlackholeArcApb::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    UMD_NOT_IMPLEMENTED();
}

bool BlackholeArcApb::is_arc_available_over_axi() { UMD_NOT_IMPLEMENTED(); }

int BlackholeArcApb::get_pcie_x_coordinate() { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
