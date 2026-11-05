// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/simulation_noc_protocol.hpp"

#include "register_access.hpp"
#include "tt-umd/tt_device/simulation_tt_device.hpp"
#include "tt-umd/types/noc_id.hpp"

namespace tt::umd {

SimulationNocProtocol::SimulationNocProtocol(SimulationTTDevice* device, int mmio_id) :
    device_(device), mmio_id_(mmio_id) {}

void SimulationNocProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    // The caller translated the coordinate for noc_id, so the access goes out on that NOC.
    NocIdSwitcher noc_switcher(noc_id);
    device_->noc_read_translated(core, addr, dst, size);
}

void SimulationNocProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    NocIdSwitcher noc_switcher(noc_id);
    device_->noc_write_translated(core, addr, src, size);
}

void SimulationNocProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    read_registers_individually(*this, dst, core, addr, size, noc_id, width);
}

void SimulationNocProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    write_registers_individually(*this, src, core, addr, size, noc_id, width);
}

bool SimulationNocProtocol::write_to_core_range(
    const void* /*src*/,
    tt_xy_pair /*core_start*/,
    tt_xy_pair /*core_end*/,
    uint64_t /*addr*/,
    size_t /*size*/,
    NocId /*noc_id*/) {
    return false;
}

int SimulationNocProtocol::get_mmio_id() { return mmio_id_; }

}  // namespace tt::umd
