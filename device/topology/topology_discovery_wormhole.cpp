/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_wormhole.h"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/types/wormhole_telemetry.h"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryWormhole::TopologyDiscoveryWormhole(
    std::unordered_set<chip_id_t> pci_target_devices, const std::string& sdesc_path) :
    TopologyDiscovery(pci_target_devices, sdesc_path) {}

TopologyDiscoveryWormhole::EthAddresses TopologyDiscoveryWormhole::get_eth_addresses(uint32_t eth_fw_version) {
    uint32_t masked_version = eth_fw_version & 0x00FFFFFF;

    uint64_t node_info;
    uint64_t eth_conn_info;
    uint64_t results_buf;
    uint64_t erisc_remote_board_type_offset;
    uint64_t erisc_local_board_type_offset;
    uint64_t erisc_local_board_id_lo_offset;
    uint64_t erisc_remote_board_id_lo_offset;
    uint64_t erisc_remote_eth_id_offset;

    if (masked_version >= 0x060000) {
        node_info = 0x1100;
        eth_conn_info = 0x1200;
        results_buf = 0x1ec0;
    } else {
        throw std::runtime_error(
            fmt::format("Unsupported ETH version {:#x}. ETH version should always be at least 6.0.0.", eth_fw_version));
    }

    if (masked_version >= 0x06C000) {
        erisc_remote_board_type_offset = 77;
        erisc_local_board_type_offset = 69;
        erisc_remote_board_id_lo_offset = 72;
        erisc_local_board_id_lo_offset = 64;
        erisc_remote_eth_id_offset = 76;
    } else {
        erisc_remote_board_type_offset = 72;
        erisc_local_board_type_offset = 64;
        erisc_remote_board_id_lo_offset = 73;
        erisc_local_board_id_lo_offset = 65;
        erisc_remote_eth_id_offset = 77;
    }

    return TopologyDiscoveryWormhole::EthAddresses{
        masked_version,
        node_info,
        eth_conn_info,
        results_buf,
        erisc_remote_board_type_offset,
        erisc_local_board_type_offset,
        erisc_local_board_id_lo_offset,
        erisc_remote_board_id_lo_offset,
        erisc_remote_eth_id_offset};
}

uint64_t TopologyDiscoveryWormhole::get_remote_board_id(Chip* chip, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // See comment in get_local_board_id.
        return get_remote_asic_id(chip, eth_core);
    }

    TTDevice* tt_device = chip->get_tt_device();
    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_id_lo_offset),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscoveryWormhole::get_local_board_id(Chip* chip, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // For 6U, since the whole trays have the same board ID, and we'd want to be able to open
        // only some chips, we hack the board_id to be the asic ID. That way, the pci_target_devices filter
        // from the ClusterOptions will work correctly on 6U.
        // Note that the board_id will still be reported properly in the cluster descriptor, since it is
        // fetched through another function when cluster descriptor is being filled up.
        return get_local_asic_id(chip, eth_core);
    }

    TTDevice* tt_device = chip->get_tt_device();
    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_local_board_id_lo_offset),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscoveryWormhole::get_remote_board_type(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_type_offset),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscoveryWormhole::get_local_asic_id(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t asic_id_lo;
    tt_device->read_from_device(
        &asic_id_lo,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_local_board_id_lo_offset),
        sizeof(uint32_t));
    uint32_t asic_id_hi;
    tt_device->read_from_device(
        &asic_id_hi,
        eth_core,
        eth_addresses.results_buf + (4 * (eth_addresses.erisc_local_board_id_lo_offset + 1)),
        sizeof(uint32_t));
    return ((static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo);
}

uint64_t TopologyDiscoveryWormhole::get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t asic_id_lo;
    tt_device->read_from_device(
        &asic_id_lo,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_id_lo_offset),
        sizeof(uint32_t));
    uint32_t asic_id_hi;
    tt_device->read_from_device(
        &asic_id_hi,
        eth_core,
        eth_addresses.results_buf + (4 * (eth_addresses.erisc_remote_board_id_lo_offset + 1)),
        sizeof(uint32_t));
    return ((static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo);
}

tt_xy_pair TopologyDiscoveryWormhole::get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) {
    const uint32_t shelf_offset = 9;
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t remote_id;
    tt_device->read_from_device(
        &remote_id,
        {local_eth_core.x, local_eth_core.y},
        eth_addresses.node_info + (4 * shelf_offset),
        sizeof(uint32_t));

    return tt_xy_pair{(remote_id >> 4) & 0x3F, (remote_id >> 10) & 0x3F};
}

uint32_t TopologyDiscoveryWormhole::read_port_status(Chip* chip, tt_xy_pair eth_core) {
    uint32_t port_status;
    uint32_t channel =
        chip->get_soc_descriptor()
            .translate_coord_to(eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::LOGICAL)
            .y;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&port_status, eth_core, eth_addresses.eth_conn_info + (channel * 4), sizeof(uint32_t));
    return port_status;
}

uint32_t TopologyDiscoveryWormhole::get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) {
    if (!is_running_on_6u) {
        throw std::runtime_error(
            "get_remote_eth_id should not be called on non-6U configurations. This message likely indicates a bug.");
    }
    uint32_t remote_eth_id;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        &remote_eth_id,
        local_eth_core,
        eth_addresses.results_buf + 4 * eth_addresses.erisc_remote_eth_id_offset,
        sizeof(uint32_t));
    return remote_eth_id;
}

std::optional<eth_coord_t> TopologyDiscoveryWormhole::get_local_eth_coord(Chip* chip) {
    std::vector<CoreCoord> eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
    if (eth_cores.empty()) {
        return std::nullopt;
    }
    TTDevice* tt_device = chip->get_tt_device();

    uint32_t current_chip_eth_coord_info;
    tt_device->read_from_device(
        &current_chip_eth_coord_info, eth_cores[0], eth_addresses.node_info + 8, sizeof(uint32_t));

    eth_coord_t eth_coord;
    eth_coord.cluster_id = 0;
    eth_coord.x = (current_chip_eth_coord_info >> 16) & 0xFF;
    eth_coord.y = (current_chip_eth_coord_info >> 24) & 0xFF;
    eth_coord.rack = current_chip_eth_coord_info & 0xFF;
    eth_coord.shelf = (current_chip_eth_coord_info >> 8) & 0xFF;

    return eth_coord;
}

std::optional<eth_coord_t> TopologyDiscoveryWormhole::get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;
    TTDevice* tt_device = chip->get_tt_device();
    eth_coord_t eth_coord;
    eth_coord.cluster_id = 0;
    uint32_t remote_id;
    tt_device->read_from_device(
        &remote_id, {eth_core.x, eth_core.y}, eth_addresses.node_info + (4 * rack_offset), sizeof(uint32_t));

    eth_coord.rack = remote_id & 0xFF;
    eth_coord.shelf = (remote_id >> 8) & 0xFF;

    tt_device->read_from_device(
        &remote_id, {eth_core.x, eth_core.y}, eth_addresses.node_info + (4 * shelf_offset), sizeof(uint32_t));

    eth_coord.x = (remote_id >> 16) & 0x3F;
    eth_coord.y = (remote_id >> 22) & 0x3F;

    return eth_coord;
}

std::unique_ptr<RemoteChip> TopologyDiscoveryWormhole::create_remote_chip(eth_coord_t eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) {
    if (is_running_on_6u) {
        return nullptr;
    }

    auto local_chip = dynamic_cast<LocalChip*>(gateway_chip);

    return RemoteChip::create(local_chip, eth_coord, gateway_eth_channels, sdesc_path);
}

uint32_t TopologyDiscoveryWormhole::get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    if (is_running_on_6u) {
        return get_remote_eth_id(chip, local_eth_core);
    }
    tt_xy_pair remote_eth_core = get_remote_eth_core(chip, local_eth_core);

    // TODO(pjanevski): explain in comment why we are using chip instead of remote chip.
    return chip->get_soc_descriptor().translate_coord_to(remote_eth_core, CoordSystem::NOC0, CoordSystem::LOGICAL).y;
}

bool TopologyDiscoveryWormhole::is_using_eth_coords() { return !is_running_on_6u; }

void TopologyDiscoveryWormhole::init_topology_discovery() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        return;
    }

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids[0]);
    tt_device->init_tt_device();
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB;
    eth_addresses = TopologyDiscoveryWormhole::get_eth_addresses(
        tt_device->get_arc_telemetry_reader()->read_entry(wormhole::ETH_FW_VERSION));
}

bool TopologyDiscoveryWormhole::is_board_id_included(uint64_t board_id, uint64_t board_type) const {
    // Since at the moment we don't want to go outside of single host on 6U,
    // we just check for board ids that are discovered from pci_target_devices.
    if (is_running_on_6u) {
        return board_ids.find(board_id) != board_ids.end();
    }

    // This is TG case, board_type is set to 0. We want to include even the TG board that is not
    // connected over PCIe, so we always want to include it.
    if (board_type == 0) {
        return true;
    }

    return board_ids.find(board_id) != board_ids.end();
}

bool TopologyDiscoveryWormhole::is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) {
    return read_port_status(chip, eth_core) == TopologyDiscoveryWormhole::ETH_UNCONNECTED;
}

bool TopologyDiscoveryWormhole::is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) {
    return read_port_status(chip, eth_core) == TopologyDiscoveryWormhole::ETH_UNKNOWN;
}

}  // namespace tt::umd
