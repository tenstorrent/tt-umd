// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

RemoteWormholeTTDevice::RemoteWormholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication) :
    WormholeTTDevice(std::move(remote_communication)) {
    // RemoteWormholeTTDevice doesn't own a PCIe/JTAG device, but some base class methods
    // (e.g. bar_read32, topology discovery) require access to the local device's interface.
    // Borrow the local device's interface until RemoteProtocol replaces this class entirely.
    bool has_pcie = false;
    try {
        TTDevice::set_pcie_interface(
            TTDevice::get_remote_interface()->get_remote_communication()->get_local_device()->get_pcie_interface());
        has_pcie = true;
    } catch (const std::runtime_error &) {
        log_warning(LogUMD, "Local device does not have a PCIe interface, trying JTAG.");
    }

    if (!has_pcie) {
        TTDevice::set_jtag_interface(
            get_remote_interface()->get_remote_communication()->get_local_device()->get_jtag_interface());
    }

    is_remote_tt_device = true;
}

void RemoteWormholeTTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    get_remote_interface()->get_remote_communication()->read_non_mmio(core, mem_ptr, addr, size);
}

void RemoteWormholeTTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    get_remote_interface()->get_remote_communication()->write_to_non_mmio(core, mem_ptr, addr, size);
}

void RemoteWormholeTTDevice::wait_for_non_mmio_flush() {
    get_remote_interface()->get_remote_communication()->wait_for_non_mmio_flush();
}

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

void RemoteWormholeTTDevice::noc_multicast_write(
    void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    // TODO: implement multicast over remote communication.
    // For now, we fallback to unicast for all cores.
    for (uint32_t x = core_start.x; x <= core_end.x; ++x) {
        for (uint32_t y = core_start.y; y <= core_end.y; ++y) {
            write_to_device(src, tt_xy_pair(x, y), addr, size);
        }
    }
}

void RemoteWormholeTTDevice::dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) {
    throw std::runtime_error("DMA write to device not supported for remote Wormhole device.");
}

void RemoteWormholeTTDevice::dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) {
    throw std::runtime_error("DMA read from device not supported for remote Wormhole device.");
}

void RemoteWormholeTTDevice::dma_multicast_write(
    void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    throw std::runtime_error("DMA multicast write not supported for remote Wormhole device.");
}

}  // namespace tt::umd
