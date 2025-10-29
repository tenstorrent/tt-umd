/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/tt_device/remote_communication_lite_fabric.hpp"

namespace tt::umd {

RemoteCommunicationLiteFabric::RemoteCommunicationLiteFabric(TTDevice* local_tt_device, SysmemManager* sysmem_manager) :
    RemoteCommunication(local_tt_device, sysmem_manager) {
    host_interface = lite_fabric::LiteFabricMemoryMap::make_host_interface(local_tt_device);
}

void RemoteCommunicationLiteFabric::read_non_mmio(
    tt_xy_pair target_core,
    void* dest,
    uint64_t core_src,
    uint32_t size_in_bytes,
    const std::chrono::milliseconds timeout_ms) {
    tt_xy_pair eth_core = get_remote_transfer_ethernet_core();
    CoreCoord core_coord = CoreCoord(eth_core.x, eth_core.y, CoreType::ETH, CoordSystem::NOC0);
    host_interface.read(dest, size_in_bytes, core_coord, target_core, core_src);
}

void RemoteCommunicationLiteFabric::write_to_non_mmio(
    tt_xy_pair target_core,
    const void* src,
    uint64_t core_dest,
    uint32_t size_in_bytes,
    bool broadcast,
    std::vector<int> broadcast_header,
    const std::chrono::milliseconds timeout_ms) {
    // hacking this to be void* from const void*
    // TODO: support const void* properly
    tt_xy_pair eth_core = get_remote_transfer_ethernet_core();
    CoreCoord core_coord = CoreCoord(eth_core.x, eth_core.y, CoreType::ETH, CoordSystem::NOC0);
    host_interface.write((void*)src, size_in_bytes, core_coord, target_core, core_dest);
}

void RemoteCommunicationLiteFabric::wait_for_non_mmio_flush(const std::chrono::milliseconds timeout_ms) {
    // TODO(pjanevski): implement this.
}

}  // namespace tt::umd
