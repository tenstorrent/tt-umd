// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/protocol/tt_sim_protocol.hpp"

#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/simulation_tt_device.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

void TTSimProtocol::attach(SimulationTTDevice* device, TTSimCommunicator* communicator, int chip_id) {
    device_ = device;
    communicator_ = communicator;
    chip_id_ = chip_id;
}

void TTSimProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId /*noc_id*/) {
    // The device's path already picks between the cached TLB window and direct tile access by
    // architecture -- Wormhole and Blackhole reach the NOC through the window, and tile access is
    // fatal on them; Quasar has no TLBs and uses tile access. Reaching that path rather than
    // duplicating it is what keeps both architectures served by one protocol.
    UMD_ASSERT(device_ != nullptr, error::RuntimeError, "TTSimProtocol used before it was attached to a device.");
    device_->noc_read_translated(core, addr, dst, size);
}

void TTSimProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId /*noc_id*/) {
    UMD_ASSERT(device_ != nullptr, error::RuntimeError, "TTSimProtocol used before it was attached to a device.");
    device_->noc_write_translated(core, addr, src, size);
}

void TTSimProtocol::read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    read_data(dst, core, addr, size, noc_id);
}

void TTSimProtocol::write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    write_data(src, core, addr, size, noc_id);
}

bool TTSimProtocol::write_to_core_range(
    const void* /*src*/,
    tt_xy_pair /*core_start*/,
    tt_xy_pair /*core_end*/,
    uint64_t /*addr*/,
    size_t /*size*/,
    NocId /*noc_id*/) {
    return false;
}

int TTSimProtocol::get_mmio_id() { return chip_id_; }

uint64_t TTSimProtocol::bar0_base() {
    if (!bar0_base_read_) {
        // Same read the device does when it starts its backend: the 64-bit BAR0 base out of config
        // space, with the attribute bits masked off.
        bar0_base_ = communicator_->pci_config_read32(0, 0x10);
        bar0_base_ |= static_cast<uint64_t>(communicator_->pci_config_read32(0, 0x14)) << 32;
        bar0_base_ &= ~15ull;
        bar0_base_read_ = true;
    }
    return bar0_base_;
}

void TTSimProtocol::bar_write32(uint32_t addr, uint32_t data) {
    communicator_->pci_mem_write_bytes(bar0_base() + addr, &data, sizeof(data));
}

uint32_t TTSimProtocol::bar_read32(uint32_t addr) {
    uint32_t data = 0;
    communicator_->pci_mem_read_bytes(bar0_base() + addr, &data, sizeof(data));
    return data;
}

int TTSimProtocol::get_numa_node() const { return -1; }

void TTSimProtocol::set_power_state(PowerState /*state*/) {
    // A simulator models no power domains. Silently ignored rather than thrown: the firmware sets a
    // power state during startup, and refusing would fail bringup over something inconsequential.
}

int TTSimProtocol::export_dmabuf(
    tt_xy_pair /*core*/, uint64_t /*addr*/, size_t /*size*/, uint64_t /*ordering*/, NocId /*noc_id*/) {
    UMD_THROW(error::RuntimeError, "Exporting a dma-buf is not supported for simulation devices.");
}

void TTSimProtocol::set_io_timeout_callback(const std::function<bool(NocId)>& /*hang_check*/) {
    // No bus to time out: an access reaches the simulator in-process or throws outright.
}

}  // namespace tt::umd
