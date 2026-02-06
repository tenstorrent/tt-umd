// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

#include "assert.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

RemoteWormholeTTDevice::RemoteWormholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication) :
    WormholeTTDevice(remote_communication->get_local_device()->get_pci_device()),
    remote_communication_(std::move(remote_communication)) {
    is_remote_tt_device = true;
}

RemoteWormholeTTDevice::RemoteWormholeTTDevice(
    std::unique_ptr<RemoteCommunication> remote_communication, IODeviceType device_type) :
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

RemoteCommunication *RemoteWormholeTTDevice::get_remote_communication() const { return remote_communication_.get(); }

void RemoteWormholeTTDevice::read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC APB address range");
    }
    read_from_device(
        mem_ptr, get_arc_core(), architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
}

void RemoteWormholeTTDevice::write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_APB_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC APB address range");
    }
    write_to_device(
        mem_ptr, get_arc_core(), architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size);
}

void RemoteWormholeTTDevice::read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC CSM address range");
    }
    read_from_device(
        mem_ptr, get_arc_core(), architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset, size);
}

void RemoteWormholeTTDevice::write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        throw std::runtime_error("Address is out of ARC CSM address range");
    }
    write_to_device(
        mem_ptr, get_arc_core(), architecture_impl_->get_arc_csm_noc_base_address() + arc_addr_offset, size);
}

void RemoteWormholeTTDevice::detect_hang_read(std::uint32_t data_read) {
    remote_communication_->get_local_device()->detect_hang_read(data_read);
}

bool RemoteWormholeTTDevice::is_hardware_hung() {
    return remote_communication_->get_local_device()->is_hardware_hung();
}

void RemoteWormholeTTDevice::noc_multicast_write(
    void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    // TODO: implement multicast over remote communication.
    // For now, we fallback to unicast for all cores.
    for (uint32_t x = core_start.x; x <= core_end.x; ++x) {
        for (uint32_t y = core_start.y; y <= core_end.y; ++y) {
            write_to_device(dst, tt_xy_pair(x, y), addr, size);
        }
    }
}

void RemoteWormholeTTDevice::dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) {
    throw std::runtime_error("DMA write to device not supported for remote Wormhole device.");
}

void RemoteWormholeTTDevice::dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) {
    throw std::runtime_error("DMA read from device not supported for remote Wormhole device.");
}

void RemoteWormholeTTDevice::l1_membar(
    const std::unordered_set<tt_xy_pair> &cores, uint32_t barrier_address, CoreType core_type) {
    wait_for_non_mmio_flush();
}

void RemoteWormholeTTDevice::dma_multicast_write(
    void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    throw std::runtime_error("DMA multicast write not supported for remote Wormhole device.");
}

}  // namespace tt::umd
