/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_wormhole.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryWormhole::TopologyDiscoveryWormhole(
    std::unordered_set<chip_id_t> target_devices, const std::string& sdesc_path, IODeviceType device_type) :
    TopologyDiscovery(target_devices, sdesc_path, device_type) {}

TopologyDiscoveryWormhole::EthAddresses TopologyDiscoveryWormhole::get_eth_addresses(uint32_t eth_fw_version) {
    uint32_t masked_version = eth_fw_version & 0x00FFFFFF;

    uint64_t eth_param_table;
    uint64_t node_info;
    uint64_t eth_conn_info;
    uint64_t results_buf;
    uint64_t erisc_remote_board_type_offset;
    uint64_t erisc_local_board_type_offset;
    uint64_t erisc_local_board_id_lo_offset;
    uint64_t erisc_remote_board_id_lo_offset;
    uint64_t erisc_remote_eth_id_offset;

    if (masked_version >= 0x060000) {
        eth_param_table = 0x1000;
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
        eth_param_table,
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

std::unique_ptr<RemoteChip> TopologyDiscoveryWormhole::create_remote_chip(
    std::optional<eth_coord_t> eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) {
    if (is_running_on_6u) {
        return nullptr;
    }
    eth_coord_t remote_chip_eth_coord = eth_coord.has_value() ? eth_coord.value() : eth_coord_t{0, 0, 0, 0};

    return RemoteChip::create(
        dynamic_cast<LocalChip*>(gateway_chip), remote_chip_eth_coord, gateway_eth_channels, sdesc_path);
}

uint32_t TopologyDiscoveryWormhole::get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    if (is_running_on_6u) {
        return get_remote_eth_id(chip, local_eth_core);
    }
    tt_xy_pair remote_eth_core = get_remote_eth_core(chip, local_eth_core);

    // TODO(pjanevski): explain in comment why we are using chip instead of remote chip.
    return chip->get_soc_descriptor().translate_coord_to(remote_eth_core, CoordSystem::NOC0, CoordSystem::LOGICAL).y;
}

uint32_t TopologyDiscoveryWormhole::get_logical_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) {
    return get_remote_eth_channel(chip, local_eth_core);
}

bool TopologyDiscoveryWormhole::is_using_eth_coords() { return !is_running_on_6u; }

void TopologyDiscoveryWormhole::init_topology_discovery() {
    int device_id = 0;
    switch (io_device_type) {
        case IODeviceType::JTAG: {
            auto device_cnt = JtagDevice::create()->get_device_cnt();
            if (!device_cnt) {
                TT_THROW("Topology discovery initialisation failed, no JTAG devices were found..");
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
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB;
    eth_addresses =
        TopologyDiscoveryWormhole::get_eth_addresses(tt_device->get_firmware_info_provider()->get_eth_fw_version());
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

std::vector<uint32_t> TopologyDiscoveryWormhole::extract_intermesh_eth_links(Chip* chip, tt_xy_pair eth_core) {
    constexpr uint32_t intermesh_eth_link_config_offset = 19;
    constexpr uint32_t intermesh_eth_link_bits_shift = 8;
    constexpr uint32_t intermesh_eth_link_bits_mask = 0xFFFF;
    TTDevice* tt_device = chip->get_tt_device();
    uint32_t config_data;
    tt_device->read_from_device(
        &config_data,
        eth_core,
        eth_addresses.eth_param_table + (4 * intermesh_eth_link_config_offset),
        sizeof(uint32_t));
    std::vector<uint32_t> intermesh_eth_links;
    uint32_t intermesh_eth_links_bits = (config_data >> intermesh_eth_link_bits_shift) & intermesh_eth_link_bits_mask;
    while (intermesh_eth_links_bits != 0) {
        uint32_t link = __builtin_ctz(intermesh_eth_links_bits);
        intermesh_eth_links.push_back(link);
        intermesh_eth_links_bits &= ~(1 << link);
    }
    return intermesh_eth_links;
}

bool TopologyDiscoveryWormhole::is_intermesh_eth_link_trained(Chip* chip, tt_xy_pair eth_core) {
    constexpr uint32_t link_status_offset = 1;
    constexpr uint32_t link_connected_mask = 0x1;
    uint32_t status;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(
        &status, eth_core, eth_addresses.node_info + (4 * link_status_offset), sizeof(uint32_t));
    return (status & link_connected_mask) == link_connected_mask;
}

void TopologyDiscoveryWormhole::verify_eth_version_local(Chip* chip) {
    std::vector<CoreCoord> eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
    for (const CoreCoord& eth_core : eth_cores) {
        uint32_t eth_fw_version_read;
        chip->read_from_device(
            eth_core, &eth_fw_version_read, chip->l1_address_params.fw_version_addr, sizeof(uint32_t));

        semver_t eth_fw_version = semver_t::from_eth_fw_tag(eth_fw_version_read);

        if (!first_eth_fw_version.has_value()) {
            log_info(LogUMD, "Established cluster ETH FW version: {}.", eth_fw_version.to_string());
            log_debug(LogUMD, "UMD supported minimum ETH FW version: {}", ERISC_FW_SUPPORTED_VERSION_MIN.to_string());
            first_eth_fw_version = eth_fw_version;
            if (ERISC_FW_SUPPORTED_VERSION_MIN.major > eth_fw_version.major) {
                TT_THROW("ETH FW major version is older than UMD supported version");
            }

            if (ERISC_FW_SUPPORTED_VERSION_MIN.minor > eth_fw_version.minor) {
                TT_THROW("ETH FW minor version is older than UMD supported version");
            }
        }

        if (eth_fw_version != first_eth_fw_version) {
            TT_THROW(
                "ETH FW version mismatch for LocalChip {} ETH core {}, found: {}.",
                get_local_asic_id(chip, eth_core),
                eth_core.str(),
                eth_fw_version.to_string());
        }
    }
}

void TopologyDiscoveryWormhole::verify_eth_version_remote(Chip* chip) {
    std::vector<CoreCoord> eth_cores =
        chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
    for (const CoreCoord& eth_core : eth_cores) {
        uint32_t eth_fw_version_read;
        chip->read_from_device(
            eth_core, &eth_fw_version_read, chip->l1_address_params.fw_version_addr, sizeof(uint32_t));
        semver_t eth_fw_version = semver_t::from_eth_fw_tag(eth_fw_version_read);

        if (eth_fw_version != first_eth_fw_version) {
            TT_THROW(
                "ETH FW version mismatch for RemoteChip ASIC ID {} ETH core {}, found: {}.",
                get_remote_asic_id(chip, eth_core),
                eth_core.str(),
                eth_fw_version.to_string());
        }
    }
}

uint64_t TopologyDiscoveryWormhole::get_unconnected_chip_id(Chip* chip) {
    return chip->get_tt_device()->get_board_id();
}

}  // namespace tt::umd
