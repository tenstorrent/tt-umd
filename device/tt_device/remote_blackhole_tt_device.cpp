// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/remote_blackhole_tt_device.hpp"

#include "umd/device/arch/blackhole_implementation.hpp"

namespace tt::umd {

RemoteBlackholeTTDevice::RemoteBlackholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication) :
    BlackholeTTDevice(remote_communication->get_local_device()->get_pci_device()),
    remote_communication_(std::move(remote_communication)) {
    is_remote_tt_device = true;
}

void RemoteBlackholeTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

void RemoteBlackholeTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void RemoteBlackholeTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    read_from_device(
        mem_ptr, get_arc_core(), architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
}

void RemoteBlackholeTTDevice::write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) {
    write_to_device(
        mem_ptr, get_arc_core(), architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
}

void RemoteBlackholeTTDevice::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

RemoteCommunication* RemoteBlackholeTTDevice::get_remote_communication() { return remote_communication_.get(); }

bool RemoteBlackholeTTDevice::wait_arc_post_reset(const std::chrono::milliseconds timeout_ms) {
    throw std::runtime_error("ARC post reset wait is not supported on remote devices.");
}

// ARC tile access over AXI is not supported for remote devices.
bool RemoteBlackholeTTDevice::is_arc_available_over_axi() { return false; }

}  // namespace tt::umd
