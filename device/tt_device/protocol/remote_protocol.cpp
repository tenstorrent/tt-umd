/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/remote_protocol.hpp"

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/ethernet_broadcast.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "utils.hpp"

namespace tt::umd {

RemoteProtocol::RemoteProtocol(std::unique_ptr<RemoteCommunication> remote_communication) :
    remote_communication_(std::move(remote_communication)) {}

void RemoteProtocol::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    remote_communication_->write_to_non_mmio(core, mem_ptr, addr, size);
}

void RemoteProtocol::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    remote_communication_->read_non_mmio(core, mem_ptr, addr, size);
}

void RemoteProtocol::write_to_device_reg(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    validate_register_access(addr, size);
    write_to_device(mem_ptr, core, addr, size, noc_id);
}

void RemoteProtocol::read_from_device_reg(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    validate_register_access(addr, size);
    read_from_device(mem_ptr, core, addr, size, noc_id);
}

bool RemoteProtocol::write_to_core_range(
    const void* mem_ptr,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t address,
    uint32_t size_in_bytes,
    NocId noc_id) {
    // Construct EthernetBroadcast lazily on the first use. During the constructor the sysmem might not be set yet.
    if (ethernet_broadcast_ == nullptr && remote_communication_->has_sysmem_manager() &&
        remote_communication_->get_local_device()->get_arch() == tt::ARCH::WORMHOLE_B0) {
        ethernet_broadcast_ = std::make_unique<EthernetBroadcast>(remote_communication_.get());
    }

    // If it is still not constructor, report failure by returning false.
    if (ethernet_broadcast_ == nullptr) {
        return false;
    }

    const bool use_translated_coords = core_start.x >= wormhole::tensix_translated_coordinate_start_x;

    std::set<uint32_t> rows_to_exclude;
    std::set<uint32_t> columns_to_exclude;

    if (!use_translated_coords) {
        // NOC0 coordinates: only the full tensix grid is supported.
        // Tensix grid is in range 1,1 to 9,11 as defined in wormhole_implementation.hpp.
        if (core_start.x > 1 || core_start.y > 1 || core_end.x < 9 || core_end.y < 11) {
            log_debug(
                LogUMD,
                "write_to_core_range: partial NOC0 range not supported for ethernet broadcast, falling back to "
                "unicast");
            return false;
        }
        // Always filter out the non-TENSIX cores.
        rows_to_exclude = {0, 6};
        columns_to_exclude = {0, 5};
    } else {
        // Translated coordinates only land correctly when NOC translation is enabled; otherwise the translated grid
        // is meaningless. Fall back to unicast rather than broadcasting to the wrong cores.
        if (!remote_communication_->get_local_device()->get_noc_translation_enabled()) {
            log_debug(
                LogUMD,
                "write_to_core_range: NOC translation is not enabled, translated ethernet broadcast not supported, "
                "falling back to unicast");
            return false;
        }
        // Always filter out the non-TENSIX cores.
        rows_to_exclude = {16, 17};
        columns_to_exclude = {16, 17};
        // Translated coordinates: exclude loop indices for any translated address outside [core_start, core_end].
        for (auto translated_x = wormhole::translated_coordinate_start_x;
             translated_x < wormhole::translated_coordinate_start_x + wormhole::GRID_SIZE.x;
             ++translated_x) {
            if (translated_x < core_start.x || translated_x > core_end.x) {
                columns_to_exclude.insert(translated_x);
            }
        }
        for (auto translated_y = wormhole::translated_coordinate_start_y;
             translated_y < wormhole::translated_coordinate_start_y + wormhole::GRID_SIZE.y;
             ++translated_y) {
            if (translated_y < core_start.y || translated_y > core_end.y) {
                rows_to_exclude.insert(translated_y);
            }
        }
    }

    ethernet_broadcast_->broadcast_write_to_cluster(
        mem_ptr, size_in_bytes, address, {}, rows_to_exclude, columns_to_exclude, use_translated_coords);

    return true;
}

int RemoteProtocol::get_mmio_id() { return remote_communication_->get_local_device()->get_communication_device_id(); }

RemoteCommunication* RemoteProtocol::get_remote_communication() { return remote_communication_.get(); }

void RemoteProtocol::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

}  // namespace tt::umd
