// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"

#include "assert.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"

namespace tt::umd {

RemoteWormholeTTDevice::RemoteWormholeTTDevice(
    std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip) :
    WormholeTTDevice(remote_communication->get_local_device()->get_pci_device()),
    target_chip_(target_chip),
    remote_communication_(std::move(remote_communication)) {
    // Since RemoteWormholeTTDevice uses RemoteCommunication and doesn't have an underlying I/O device,
    // which in turn uses a local TTDevice for communication,
    // the device type of the underlying communication device is the device type of the local TTDevice.
    communication_device_type_ = remote_communication_->get_local_device()->get_communication_device_type();
    communication_device_id_ = remote_communication_->get_local_device()->get_communication_device_id();
    is_remote_tt_device = true;
}

void RemoteWormholeTTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

void RemoteWormholeTTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void RemoteWormholeTTDevice::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

RemoteCommunication *RemoteWormholeTTDevice::get_remote_communication() { return remote_communication_.get(); }

void RemoteWormholeTTDevice::read_from_arc(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_XBAR_ADDRESS_END) {
        throw std::runtime_error("Address is out of ARC XBAR address range");
    }
    read_from_device(mem_ptr, get_arc_core(), get_arc_noc_base_address() + arc_addr_offset, size);
}

void RemoteWormholeTTDevice::write_to_arc(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_XBAR_ADDRESS_END) {
        throw std::runtime_error("Address is out of ARC XBAR address range");
    }
    write_to_device(mem_ptr, get_arc_core(), get_arc_noc_base_address() + arc_addr_offset, size);
}

bool RemoteWormholeTTDevice::wait_arc_post_reset(const uint32_t timeout_ms) { return true; }

void RemoteWormholeTTDevice::detect_hang_read(std::uint32_t data_read) {
    remote_communication_->get_local_device()->detect_hang_read(data_read);
}

bool RemoteWormholeTTDevice::is_hardware_hung() {
    return remote_communication_->get_local_device()->is_hardware_hung();
}

}  // namespace tt::umd
