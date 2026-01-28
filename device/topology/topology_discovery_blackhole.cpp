// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/topology_discovery_blackhole.hpp"

#include <optional>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(const TopologyDiscoveryOptions& options) :
    TopologyDiscovery(options) {}

std::unique_ptr<TTDevice> TopologyDiscoveryBlackhole::create_remote_device(
    std::optional<EthCoord> eth_coord, TTDevice* gateway_device, std::set<uint32_t> gateway_eth_channels) {
    // ETH coord is not used for Blackhole, as Blackhole does not have a concept of ETH coordinates.
    std::unique_ptr<RemoteCommunication> remote_communication =
        RemoteCommunication::create_remote_communication(gateway_device, {0, 0, 0, 0});
    remote_communication->set_remote_transfer_ethernet_cores(
        get_soc_descriptor(gateway_device)
            .get_eth_xy_pairs_for_channels(gateway_eth_channels, CoordSystem::TRANSLATED));
    std::unique_ptr<TTDevice> remote_tt_device = TTDevice::create(std::move(remote_communication));
    remote_tt_device->init_tt_device();
    return remote_tt_device;
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_local_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) {
    return std::nullopt;
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_remote_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) {
    return std::nullopt;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // See comment in get_local_board_id.
        return get_remote_asic_id(tt_device, eth_core);
    }

    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFE8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFE4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // For 6U, since the whole trays have the same board ID, and we'd want to be able to open
        // only some chips, we hack the board_id to be the asic ID. That way, the pci_target_devices filter
        // from the ClusterOptions will work correctly on 6U.
        // Note that the board_id will still be reported properly in the cluster descriptor, since it is
        // fetched through another function when cluster descriptor is being filled up.
        return get_local_asic_id(tt_device, eth_core);
    }

    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFC8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFC4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

    if (is_running_on_6u) {
        uint32_t asic_id_hi;
        tt_device->read_from_device(&asic_id_hi, translated_eth_core, 0x7CFD4, sizeof(asic_id_hi));

        uint32_t asic_id_lo;
        tt_device->read_from_device(&asic_id_lo, translated_eth_core, 0x7CFD8, sizeof(asic_id_lo));

        return ((uint64_t)asic_id_hi << 32) | asic_id_lo;
    }

    uint64_t board_id = get_local_board_id(tt_device, eth_core);
    uint8_t asic_location;
    tt_device->read_from_device(&asic_location, translated_eth_core, 0x7CFC1, sizeof(asic_location));

    return mangle_asic_id(board_id, asic_location);
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

    if (is_running_on_6u) {
        uint32_t asic_id_hi;
        tt_device->read_from_device(&asic_id_hi, translated_eth_core, 0x7CFF4, sizeof(asic_id_hi));

        uint32_t asic_id_lo;
        tt_device->read_from_device(&asic_id_lo, translated_eth_core, 0x7CFF8, sizeof(asic_id_lo));

        return ((uint64_t)asic_id_hi << 32) | asic_id_lo;
    }

    uint64_t board_id = get_remote_board_id(tt_device, eth_core);
    uint8_t asic_location;
    tt_device->read_from_device(&asic_location, translated_eth_core, 0x7CFE1, sizeof(asic_location));

    return mangle_asic_id(board_id, asic_location);
}

tt_xy_pair TopologyDiscoveryBlackhole::get_remote_eth_core(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    throw std::runtime_error(
        "get_remote_eth_core is not implemented for Blackhole. Calling this function for Blackhole likely indicates a "
        "bug.");
}

uint32_t TopologyDiscoveryBlackhole::read_port_status(TTDevice* tt_device, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t port_status;
    tt_device->read_from_device(&port_status, translated_eth_core, 0x7CC04, sizeof(port_status));
    return port_status;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_id(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        local_eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t remote_eth_id;
    tt_device->read_from_device(&remote_eth_id, translated_eth_core, 0x7CFE2, sizeof(remote_eth_id));
    return remote_eth_id;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_type(TTDevice* tt_device, tt_xy_pair eth_core) {
    // This function is not important for Blackhole, so we can return any value here.
    return 0;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    return get_remote_eth_id(tt_device, local_eth_core);
}

uint32_t TopologyDiscoveryBlackhole::get_logical_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    auto translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        local_eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t remote_logical_eth_id;
    tt_device->read_from_device(&remote_logical_eth_id, translated_eth_core, 0x7CFE3, sizeof(remote_logical_eth_id));

    // For FW Versions older than 18.12.0, querying remote eth channels in logical space is only supported
    // for P150 Board Types (with a  SW workaround).
    if (first_fw_bundle_version >= semver_t(18, 12, 0)) {
        return remote_logical_eth_id;
    }
    if (tt_device->get_chip_info().board_type != BoardType::P150) {
        throw std::runtime_error(
            "Querying Logical Eth Channels on a Remote Host is only supported for P150 Board Types.");
    }
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

bool TopologyDiscoveryBlackhole::is_eth_trained(TTDevice* tt_device, const tt_xy_pair eth_core) {
    return read_port_status(tt_device, eth_core) == blackhole::port_status_e::PORT_UP;
}

void TopologyDiscoveryBlackhole::patch_eth_connections() {
    std::set<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections_fixed;
    for (auto& eth_connections_original : ethernet_connections) {
        auto& [local_device, local_channel] = eth_connections_original.first;
        auto& [remote_device, remote_channel] = eth_connections_original.second;

        TTDevice* remote_device_ptr = get_tt_device(remote_device);

        auto eth_core_noc0 = blackhole::ETH_CORES_NOC0[remote_channel];
        CoreCoord eth_core_coord = CoreCoord(eth_core_noc0.x, eth_core_noc0.y, CoreType::ETH, CoordSystem::NOC0);
        CoreCoord logical_coord =
            get_soc_descriptor(remote_device_ptr).translate_coord_to(eth_core_coord, CoordSystem::LOGICAL);

        ethernet_connections_fixed.insert({{local_device, local_channel}, {remote_device, logical_coord.y}});
    }

    ethernet_connections.clear();
    for (auto& eth_connections_fixed : ethernet_connections_fixed) {
        auto& [local_device, local_channel] = eth_connections_fixed.first;
        auto& [remote_device, remote_channel] = eth_connections_fixed.second;
        ethernet_connections.push_back({{local_device, local_channel}, {remote_device, remote_channel}});
    }
}

void TopologyDiscoveryBlackhole::init_first_device(TTDevice* tt_device) {
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB_BLACKHOLE;
}

bool TopologyDiscoveryBlackhole::verify_eth_core_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    static constexpr uint64_t eth_fw_major_addr = 0x7CFBE;
    static constexpr uint64_t eth_fw_minor_addr = 0x7CFBD;
    static constexpr uint64_t eth_fw_patch_addr = 0x7CFBC;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;

    tt_device->read_from_device(&major, translated_eth_core, eth_fw_major_addr, sizeof(uint8_t));
    tt_device->read_from_device(&minor, translated_eth_core, eth_fw_minor_addr, sizeof(uint8_t));
    tt_device->read_from_device(&patch, translated_eth_core, eth_fw_patch_addr, sizeof(uint8_t));
    semver_t eth_fw_version = semver_t(major, minor, patch);

    bool eth_fw_problem = false;
    if (!expected_eth_fw_version.has_value()) {
        expected_eth_fw_version =
            get_expected_eth_firmware_version_from_firmware_bundle(first_fw_bundle_version.value(), ARCH::BLACKHOLE);
        if (options.predict_eth_fw_version && expected_eth_fw_version.has_value()) {
            log_debug(LogUMD, "Expected ETH FW version: {}", expected_eth_fw_version->to_string());
        } else {
            expected_eth_fw_version = eth_fw_version;
            log_debug(
                LogUMD, "Established ETH FW version from first discovered ETH core: {}", eth_fw_version.to_string());
        }
        if (erisc_firmware::BH_MIN_ERISC_FW_SUPPORTED_VERSION > eth_fw_version) {
            log_warning(LogUMD, "ETH FW version is older than UMD supported version");
            eth_fw_problem = true;
        }
    }

    if (eth_fw_version != expected_eth_fw_version) {
        log_warning(
            LogUMD,
            "ETH FW version mismatch for device {} ETH core {}, found: {}.",
            get_local_asic_id(tt_device, eth_core),
            eth_core.str(),
            eth_fw_version.to_string());
        eth_fw_problem = true;
    }

    if (options.verify_eth_fw_hash && !tt_device->is_remote()) {
        auto hash_check = verify_eth_fw_integrity(tt_device, translated_eth_core, eth_fw_version);
        if (hash_check.has_value() && hash_check.value() == false) {
            log_warning(
                LogUMD,
                "ETH FW version hash check failed for device {} ETH core {}",
                get_local_asic_id(tt_device, eth_core),
                eth_core.str());
            eth_fw_problem = true;
        }
    }

    return options.no_eth_firmware_strictness || !eth_fw_problem;
}

uint64_t TopologyDiscoveryBlackhole::get_unconnected_device_id(TTDevice* tt_device) {
    uint32_t asic_id_lo = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_ID_LOW);
    uint32_t asic_id_hi = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_ID_HIGH);
    return (static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo;
}

bool TopologyDiscoveryBlackhole::verify_routing_firmware_state(TTDevice* tt_device, const tt_xy_pair eth_core) {
    return true;
}

}  // namespace tt::umd
