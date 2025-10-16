/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_blackhole.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/chip/remote_chip.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/lite_fabric/lite_fabric_host_utils.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/cluster_types.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(
    std::unordered_set<chip_id_t> pci_target_devices, const std::string& sdesc_path, bool disable_wait_on_eth_core_training, bool break_ports) :
    TopologyDiscovery(pci_target_devices, sdesc_path, IODeviceType::PCIe, disable_wait_on_eth_core_training, break_ports) {}

std::unique_ptr<RemoteChip> TopologyDiscoveryBlackhole::create_remote_chip(
    std::optional<eth_coord_t> eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels, bool disable_wait_on_eth_core_training) {
    // ETH coord is not used for Blackhole, as Blackhole does not have a concept of ETH coordinates.
    return RemoteChip::create(dynamic_cast<LocalChip*>(gateway_chip), {0, 0, 0, 0}, gateway_eth_channels, sdesc_path, disable_wait_on_eth_core_training);
}

std::optional<eth_coord_t> TopologyDiscoveryBlackhole::get_local_eth_coord(Chip* chip) { return std::nullopt; }

std::optional<eth_coord_t> TopologyDiscoveryBlackhole::get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    return std::nullopt;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(Chip* chip, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // See comment in get_local_board_id.
        return get_remote_asic_id(chip, eth_core);
    }

    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFE8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFE4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(Chip* chip, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // For 6U, since the whole trays have the same board ID, and we'd want to be able to open
        // only some chips, we hack the board_id to be the asic ID. That way, the pci_target_devices filter
        // from the ClusterOptions will work correctly on 6U.
        // Note that the board_id will still be reported properly in the cluster descriptor, since it is
        // fetched through another function when cluster descriptor is being filled up.
        return get_local_asic_id(chip, eth_core);
    }

    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFC8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFC4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

    TTDevice* tt_device = chip->get_tt_device();

    if (is_running_on_6u) {
        uint32_t asic_id_hi;
        tt_device->read_from_device(&asic_id_hi, translated_eth_core, 0x7CFD4, sizeof(asic_id_hi));

        uint32_t asic_id_lo;
        tt_device->read_from_device(&asic_id_lo, translated_eth_core, 0x7CFD8, sizeof(asic_id_lo));

        return ((uint64_t)asic_id_hi << 32) | asic_id_lo;
    }

    uint64_t board_id = get_local_board_id(chip, eth_core);
    uint8_t asic_location;
    tt_device->read_from_device(&asic_location, translated_eth_core, 0x7CFC1, sizeof(asic_location));

    return mangle_asic_id(board_id, asic_location);
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

    TTDevice* tt_device = chip->get_tt_device();

    if (is_running_on_6u) {
        uint32_t asic_id_hi;
        tt_device->read_from_device(&asic_id_hi, translated_eth_core, 0x7CFF4, sizeof(asic_id_hi));

        uint32_t asic_id_lo;
        tt_device->read_from_device(&asic_id_lo, translated_eth_core, 0x7CFF8, sizeof(asic_id_lo));

        return ((uint64_t)asic_id_hi << 32) | asic_id_lo;
    }

    uint64_t board_id = get_remote_board_id(chip, eth_core);
    uint8_t asic_location;
    tt_device->read_from_device(&asic_location, translated_eth_core, 0x7CFE1, sizeof(asic_location));

    return mangle_asic_id(board_id, asic_location);
}

tt_xy_pair TopologyDiscoveryBlackhole::get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) {
    throw std::runtime_error(
        "get_remote_eth_core is not implemented for Blackhole. Calling this function for Blackhole likely indicates a "
        "bug.");
}

uint32_t TopologyDiscoveryBlackhole::read_training_status(Chip* chip, tt_xy_pair eth_core) {
    return 0;
}

uint32_t TopologyDiscoveryBlackhole::read_port_status(Chip* chip, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t port_status;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&port_status, translated_eth_core, 0x7CC04, sizeof(port_status));
    return port_status;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) {
    tt_xy_pair translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        local_eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    TTDevice* tt_device = chip->get_tt_device();
    uint8_t remote_eth_id;
    tt_device->read_from_device(&remote_eth_id, translated_eth_core, 0x7CFE2, sizeof(remote_eth_id));
    return remote_eth_id;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_type(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole, so we can return any value here.
    return 0;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    return get_remote_eth_id(chip, local_eth_core);
}

uint32_t TopologyDiscoveryBlackhole::get_logical_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    if (chip->get_chip_info().board_type != BoardType::P150) {
        throw std::runtime_error(
            "Querying Logical Eth Channels on a Remote Host is only supported for P150 Board Types.");
    }
    auto translated_eth_core = chip->get_soc_descriptor().translate_coord_to(
        local_eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t remote_logical_eth_id;
    chip->get_tt_device()->read_from_device(
        &remote_logical_eth_id, translated_eth_core, 0x7CFE3, sizeof(remote_logical_eth_id));
    // Adding 4 here, since for P150, the logical eth chan id stored at address 0x7CFE3 hides
    // the first 4 ethernet channels (these channels are using SerDes for PCIe)
    // These channels are visible to UMD, and are thus accounted for in this API.
    return remote_logical_eth_id + 4;
}

bool TopologyDiscoveryBlackhole::is_using_eth_coords() { return false; }

bool TopologyDiscoveryBlackhole::is_board_id_included(uint64_t board_id, uint64_t board_type) const {
    return board_ids.find(board_id) != board_ids.end();
}

uint64_t TopologyDiscoveryBlackhole::mangle_asic_id(uint64_t board_id, uint8_t asic_location) {
    return ((board_id << 5) | (asic_location & 0x1F));
}

bool TopologyDiscoveryBlackhole::is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) {
    return read_port_status(chip, eth_core) == blackhole::port_status_e::PORT_UNUSED;
}

bool TopologyDiscoveryBlackhole::is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) {
    uint32_t port_status = read_port_status(chip, eth_core);
    return port_status == blackhole::port_status_e::PORT_UNKNOWN || port_status == blackhole::port_status_e::PORT_DOWN;
}

void TopologyDiscoveryBlackhole::patch_eth_connections() {
    std::set<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections_fixed;
    for (auto& eth_connections_original : ethernet_connections) {
        auto& [local_chip, local_channel] = eth_connections_original.first;
        auto& [remote_chip, remote_channel] = eth_connections_original.second;

        Chip* remote_chip_ptr = get_chip(remote_chip);

        auto eth_core_noc0 = tt::umd::blackhole::ETH_CORES_NOC0[remote_channel];
        CoreCoord eth_core_coord = CoreCoord(eth_core_noc0.x, eth_core_noc0.y, CoreType::ETH, CoordSystem::NOC0);
        CoreCoord logical_coord =
            remote_chip_ptr->get_soc_descriptor().translate_coord_to(eth_core_coord, CoordSystem::LOGICAL);

        ethernet_connections_fixed.insert({{local_chip, local_channel}, {remote_chip, logical_coord.y}});
    }

    ethernet_connections.clear();
    for (auto& eth_connections_fixed : ethernet_connections_fixed) {
        auto& [local_chip, local_channel] = eth_connections_fixed.first;
        auto& [remote_chip, remote_channel] = eth_connections_fixed.second;
        ethernet_connections.push_back({{local_chip, local_channel}, {remote_chip, remote_channel}});
    }
}

std::vector<uint32_t> TopologyDiscoveryBlackhole::extract_intermesh_eth_links(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole.
    return {};
}

bool TopologyDiscoveryBlackhole::is_intermesh_eth_link_trained(Chip* chip, tt_xy_pair eth_core) {
    // This function is not important for Blackhole.
    return false;
}

void TopologyDiscoveryBlackhole::initialize_remote_communication(Chip* chip) {
    // We don't want to initialize lite fabric on non-P300 boards. For all configurations we have at the moment,
    // we would need to init lite fabric just on LocalChips of P300 boards.
    // TODO: Think about future configurations where we might want to init lite fabric on other boards as well.
    if (chip->get_tt_device()->get_board_type() != BoardType::P300) {
        return;
    }

    auto eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);

    std::unordered_map<uint64_t, std::vector<CoreCoord>> remote_asic_ids_to_eth_cores;

    for (const auto& eth_core : eth_cores) {
        uint32_t port_status = read_port_status(chip, eth_core);

        if (is_eth_unknown(chip, eth_core) || is_eth_unconnected(chip, eth_core)) {
            continue;
        }

        uint64_t remote_asic_id = get_remote_asic_id(chip, eth_core);
        remote_asic_ids_to_eth_cores[remote_asic_id].push_back(eth_core);
    }

    // TODO: be careful to not launch lite fabric on ETH cores that already have it running.
    for (const auto& [remote_asic_id, eth_cores] : remote_asic_ids_to_eth_cores) {
        lite_fabric::launch_lite_fabric(chip, eth_cores);
    }
}

void TopologyDiscoveryBlackhole::init_topology_discovery() {
    int device_id = 0;
    switch (io_device_type) {
        case IODeviceType::JTAG: {
            auto device_cnt = JtagDevice::create()->get_device_cnt();
            if (!device_cnt) {
                return;
            }
            // JTAG devices (j-links) are referred to with their index within a vector
            // that's stored inside of a JtagDevice object.
            // That index is completely different from the actual JTAG device id.
            // So no matter how many JTAG devices (j-links) are present, the one with index 0 will be used here.
            break;
        }
        case IODeviceType::PCIe: {
            std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
            if (pci_device_ids.empty()) {
                return;
            }
            device_id = pci_device_ids[0];
            break;
        }
        default:
            TT_THROW("Unsupported IODeviceType during topology discovery.");
    }

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_id, io_device_type);
    tt_device->init_tt_device();
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB_BLACKHOLE;
}

}  // namespace tt::umd
