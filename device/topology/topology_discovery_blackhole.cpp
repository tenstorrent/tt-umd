/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_blackhole.h"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/remote_chip.h"
#include "umd/device/remote_communication.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"
#include "umd/device/types/blackhole_eth.h"
#include "umd/device/types/cluster_types.h"
#include "umd/device/types/wormhole_telemetry.h"
#include "umd/device/wormhole_implementation.h"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(
    std::unordered_set<chip_id_t> pci_target_devices, const std::string& sdesc_path) :
    TopologyDiscovery(pci_target_devices, sdesc_path) {}

std::unique_ptr<RemoteChip> TopologyDiscoveryBlackhole::create_remote_chip(Chip* gateway_chip, CoreCoord eth_core) {
    return nullptr;
}

std::optional<eth_coord_t> TopologyDiscoveryBlackhole::get_local_eth_coord(Chip* chip) { return std::nullopt; }

std::optional<eth_coord_t> TopologyDiscoveryBlackhole::get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    return std::nullopt;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(Chip* chip, tt_xy_pair eth_core) {
    blackhole::boot_results_t boot_results;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        (uint8_t*)&boot_results,
        tt_xy_pair(eth_core.x, eth_core.y),
        blackhole::BOOT_RESULTS_ADDR,
        sizeof(boot_results));

    return ((uint64_t)boot_results.remote_info.board_id_hi << 32) | boot_results.remote_info.board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(Chip* chip, tt_xy_pair eth_core) {
    blackhole::boot_results_t boot_results;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        (uint8_t*)&boot_results,
        tt_xy_pair(eth_core.x, eth_core.y),
        blackhole::BOOT_RESULTS_ADDR,
        sizeof(boot_results));

    return ((uint64_t)boot_results.local_info.board_id_hi << 32) | boot_results.local_info.board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(Chip* chip, tt_xy_pair eth_core) {
    blackhole::boot_results_t boot_results;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        (uint8_t*)&boot_results,
        tt_xy_pair(eth_core.x, eth_core.y),
        blackhole::BOOT_RESULTS_ADDR,
        sizeof(boot_results));

    uint64_t board_id = ((uint64_t)boot_results.local_info.board_id_hi << 32) | boot_results.local_info.board_id_lo;
    return mangle_asic_id(board_id, boot_results.local_info.asic_location);
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) {
    blackhole::boot_results_t boot_results;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        (uint8_t*)&boot_results,
        tt_xy_pair(eth_core.x, eth_core.y),
        blackhole::BOOT_RESULTS_ADDR,
        sizeof(boot_results));

    uint64_t board_id = ((uint64_t)boot_results.remote_info.board_id_hi << 32) | boot_results.remote_info.board_id_lo;

    return mangle_asic_id(board_id, boot_results.remote_info.asic_location);
}

tt_xy_pair TopologyDiscoveryBlackhole::get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) {
    throw std::runtime_error(
        "get_remote_eth_core is not implemented for Blackhole. Calling this function for Blackhole likely indicates a "
        "bug.");
}

uint32_t TopologyDiscoveryBlackhole::read_port_status(Chip* chip, tt_xy_pair eth_core) {
    blackhole::boot_results_t boot_results;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        (uint8_t*)&boot_results,
        tt_xy_pair(eth_core.x, eth_core.y),
        blackhole::BOOT_RESULTS_ADDR,
        sizeof(boot_results));
    return boot_results.eth_status.port_status;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) {
    // 7CFC3
    uint8_t remote_eth_id;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        &remote_eth_id,
        local_eth_core,
        0x7CFC3,
        sizeof(remote_eth_id));
    
    std::cout << "remote eth id " << (uint32_t)remote_eth_id << std::endl;
    return remote_eth_id;

    // blackhole::boot_results_t boot_results;
    // TTDevice* tt_device = chip->get_tt_device();
    // tt_device->read_from_device(
    //     (uint8_t*)&boot_results, local_eth_core, blackhole::BOOT_RESULTS_ADDR, sizeof(boot_results));
    //     auto x = boot_results.local_info.board_id_hi;
    //     auto y = boot_results.local_info.board_id_lo;
    //     uint64_t board_id = ((uint64_t)x << 32) | y;
    //     // return x;
    //     std::cout << "x " << x << std::endl;
    // std::cout << "remote eth id " << (uint32_t)boot_results.remote_info.logical_eth_id << std::endl;
    // return boot_results.remote_info.logical_eth_id;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_type(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole, so we can return any value here.
    return 0;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    auto remote_eth_id = get_remote_eth_id(chip, local_eth_core);
    std::cout << "returned remote eth id " << remote_eth_id << std::endl;
    return remote_eth_id;
}

bool TopologyDiscoveryBlackhole::is_using_eth_coords() { return false; }

bool TopologyDiscoveryBlackhole::is_board_id_included(uint64_t board_id, uint64_t board_type) const {
    return board_ids.find(board_id) != board_ids.end();
}

uint64_t TopologyDiscoveryBlackhole::mangle_asic_id(uint64_t board_id, uint8_t asic_location) {
    return ((board_id << 1) | (asic_location & 0x1));
}

bool TopologyDiscoveryBlackhole::is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) {
    return read_port_status(chip, eth_core) == blackhole::port_status_e::PORT_UNUSED;
}

bool TopologyDiscoveryBlackhole::is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) {
    uint32_t port_status = read_port_status(chip, eth_core);
    return port_status == blackhole::port_status_e::PORT_UNKNOWN || port_status == blackhole::port_status_e::PORT_DOWN;
}

}  // namespace tt::umd
