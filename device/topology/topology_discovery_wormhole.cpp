/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_wormhole.hpp"

#include <optional>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/utils/semver.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryWormhole::TopologyDiscoveryWormhole(const TopologyDiscoveryOptions& options) :
    TopologyDiscovery(options) {}

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
    uint64_t routing_firmware_state;

    if (masked_version >= 0x060000) {
        eth_param_table = 0x1000;
        node_info = 0x1100;
        eth_conn_info = 0x1200;
        results_buf = 0x1ec0;
        routing_firmware_state = 0x104c;
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
        routing_firmware_state,
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

uint32_t TopologyDiscoveryWormhole::read_training_status(Chip* chip, tt_xy_pair eth_core) {
    uint32_t training_status;
    TTDevice* tt_device = chip->get_tt_device();
    tt_device->read_from_device(&training_status, eth_core, 0x1104, sizeof(uint32_t));
    return training_status;
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

std::optional<EthCoord> TopologyDiscoveryWormhole::get_local_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    TTDevice* tt_device = chip->get_tt_device();

    uint32_t current_chip_eth_coord_info;
    tt_device->read_from_device(&current_chip_eth_coord_info, eth_core, eth_addresses.node_info + 8, sizeof(uint32_t));

    EthCoord eth_coord;
    eth_coord.cluster_id = 0;
    eth_coord.x = (current_chip_eth_coord_info >> 16) & 0xFF;
    eth_coord.y = (current_chip_eth_coord_info >> 24) & 0xFF;
    eth_coord.rack = current_chip_eth_coord_info & 0xFF;
    eth_coord.shelf = (current_chip_eth_coord_info >> 8) & 0xFF;

    return eth_coord;
}

std::optional<EthCoord> TopologyDiscoveryWormhole::get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) {
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;
    TTDevice* tt_device = chip->get_tt_device();
    EthCoord eth_coord;
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
    std::optional<EthCoord> eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) {
    if (is_running_on_6u) {
        return nullptr;
    }
    EthCoord remote_chip_eth_coord = eth_coord.has_value() ? eth_coord.value() : EthCoord{0, 0, 0, 0};

    return RemoteChip::create(
        dynamic_cast<LocalChip*>(gateway_chip),
        remote_chip_eth_coord,
        gateway_eth_channels,
        options.soc_descriptor_path);
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
    std::vector<int> device_ids;
    switch (options.io_device_type) {
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
            auto pci_device_ids = PCIDevice::enumerate_devices();
            if (pci_device_ids.empty()) {
                return;
            }
            device_ids = pci_device_ids;
            break;
        }
        default:
            TT_THROW("Unsupported IODeviceType during topology discovery.");
    }

    for (auto& device_id : device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_id, options.io_device_type);
        // When coming out of reset, devices can take on the order of minutes to become ready.
        tt_device->init_tt_device(timeout::ARC_LONG_POST_RESET_TIMEOUT);
    }

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_ids[0], options.io_device_type);
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

bool TopologyDiscoveryWormhole::is_eth_trained(Chip* chip, const tt_xy_pair eth_core) {
    return read_training_status(chip, eth_core) == LINK_TRAIN_SUCCESS;
}

bool TopologyDiscoveryWormhole::verify_eth_core_fw_version(Chip* chip, CoreCoord eth_core) {
    uint32_t eth_fw_version_read;
    chip->read_from_device(eth_core, &eth_fw_version_read, chip->l1_address_params.fw_version_addr, sizeof(uint32_t));

    semver_t eth_fw_version = semver_t::from_eth_fw_tag(eth_fw_version_read);

    bool eth_fw_problem = false;
    if (!expected_eth_fw_version.has_value()) {
        log_info(LogUMD, "Established ETH FW version from first discovered ETH core: {}", eth_fw_version.to_string());
        expected_eth_fw_version = eth_fw_version;
        if (WH_ERISC_FW_SUPPORTED_VERSION_MIN > eth_fw_version) {
            log_warning(LogUMD, "ETH FW version is older than UMD supported version");
            eth_fw_problem = true;
        }
    }

    if (eth_fw_version != expected_eth_fw_version) {
        log_warning(
            LogUMD,
            "ETH FW version mismatch for chip {} ETH core {}, found: {}.",
            get_local_asic_id(chip, eth_core),
            eth_core.str(),
            eth_fw_version.to_string());
        eth_fw_problem = true;
    }

    return options.no_eth_firmware_strictness || !eth_fw_problem;
}

std::optional<semver_t> TopologyDiscoveryWormhole::get_expected_erisc_fw_version_from_fw_bundle(
    semver_t fw_bundle_version) const {
    if (fw_bundle_version.major >= 80) {
        if (fw_bundle_version >= WH_LEGACY_ERISC_FW_VERSION_MAP[0].first) {
            return WH_ERISC_FW_VERSION_MAP[0].second;
        }
        return std::nullopt;
    }
    auto it = std::upper_bound(
        WH_ERISC_FW_VERSION_MAP.begin(),
        WH_ERISC_FW_VERSION_MAP.end(),
        fw_bundle_version,
        [](const semver_t& version, const std::pair<semver_t, semver_t>& entry) { return version < entry.first; });

    if (it != WH_ERISC_FW_VERSION_MAP.begin()) {
        --it;
        return it->second;
    }
    return std::nullopt;
}

uint64_t TopologyDiscoveryWormhole::get_unconnected_chip_id(Chip* chip) {
    return chip->get_tt_device()->get_board_id();
}

void TopologyDiscoveryWormhole::validate_routing_firmware_state(
    const std::map<uint64_t, std::unique_ptr<Chip>>& chips) {
    for (const auto& [asic_id, chip] : chips) {
        std::vector<CoreCoord> eth_cores =
            chip->get_soc_descriptor().get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);
        if (eth_cores.empty()) {
            continue;
        }
        TTDevice* tt_device = chip->get_tt_device();

        uint32_t routing_firmware_disabled;
        tt_device->read_from_device(
            &routing_firmware_disabled, eth_cores[0], eth_addresses.routing_firmware_state, sizeof(uint32_t));
        if (is_running_on_6u && routing_firmware_disabled == 0) {
            throw std::runtime_error("Routing Firmware should not be enabled on 6U-Galaxy Systems.");
        } else if (!is_running_on_6u && routing_firmware_disabled == 1) {
            throw std::runtime_error("Routing Firmware should be enabled on Non 6U-Galaxy Systems.");
        }
    }
}

}  // namespace tt::umd
