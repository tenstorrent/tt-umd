/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/topology/topology_discovery_blackhole.hpp"

#include <memory>
#include <optional>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/chip/remote_chip.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/lite_fabric/lite_fabric_host_utils.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/xy_pair.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(const TopologyDiscoveryOptions& options) :
    TopologyDiscovery(options) {}

std::unique_ptr<TTDevice> TopologyDiscoveryBlackhole::create_remote_chip(
    std::optional<EthCoord> eth_coord, TTDevice* gateway_chip, std::set<uint32_t> gateway_eth_channels) {
    // ETH coord is not used for Blackhole, as Blackhole does not have a concept of ETH coordinates.
    std::unique_ptr<RemoteCommunication> remote_communication =
        RemoteCommunication::create_remote_communication(gateway_chip, {0, 0, 0, 0});
    remote_communication->set_remote_transfer_ethernet_cores(
        get_soc_descriptor(gateway_chip).get_eth_xy_pairs_for_channels(gateway_eth_channels, CoordSystem::TRANSLATED));
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
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
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
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint32_t board_id_lo;
    tt_device->read_from_device(&board_id_lo, translated_eth_core, 0x7CFC8, sizeof(board_id_lo));

    uint32_t board_id_hi;
    tt_device->read_from_device(&board_id_hi, translated_eth_core, 0x7CFC4, sizeof(board_id_hi));

    return (static_cast<uint64_t>(board_id_hi) << 32) | board_id_lo;
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

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
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);

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
        eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t port_status;
    tt_device->read_from_device(&port_status, translated_eth_core, 0x7CC04, sizeof(port_status));
    return port_status;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_id(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        local_eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
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
    if (tt_device->get_board_type() != BoardType::P150) {
        throw std::runtime_error(
            "Querying Logical Eth Channels on a Remote Host is only supported for P150 Board Types.");
    }
    auto translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
        local_eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
    uint8_t remote_logical_eth_id;
    tt_device->read_from_device(&remote_logical_eth_id, translated_eth_core, 0x7CFE3, sizeof(remote_logical_eth_id));

    auto fw_bundle_version = tt_device->get_firmware_version();

    if (fw_bundle_version >= semver_t(18, 12, 0)) {
        return remote_logical_eth_id;
    } else {
        // Adding 4 here, since for P150, the logical eth chan id stored at address 0x7CFE3 hides
        // the first 4 ethernet channels (these channels are using SerDes for PCIe)
        // These channels are visible to UMD, and are thus accounted for in this API.
        return remote_logical_eth_id + 4;
    }
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
        auto& [local_chip, local_channel] = eth_connections_original.first;
        auto& [remote_chip, remote_channel] = eth_connections_original.second;

        TTDevice* remote_chip_ptr = get_chip(remote_chip);

        auto eth_core_noc0 = blackhole::ETH_CORES_NOC0[remote_channel];
        CoreCoord eth_core_coord = CoreCoord(eth_core_noc0.x, eth_core_noc0.y, CoreType::ETH, CoordSystem::NOC0);
        CoreCoord logical_coord =
            get_soc_descriptor(remote_chip_ptr).translate_coord_to(eth_core_coord, CoordSystem::LOGICAL);

        ethernet_connections_fixed.insert({{local_chip, local_channel}, {remote_chip, logical_coord.y}});
    }

    ethernet_connections.clear();
    for (auto& eth_connections_fixed : ethernet_connections_fixed) {
        auto& [local_chip, local_channel] = eth_connections_fixed.first;
        auto& [remote_chip, remote_channel] = eth_connections_fixed.second;
        ethernet_connections.push_back({{local_chip, local_channel}, {remote_chip, remote_channel}});
    }
}

void TopologyDiscoveryBlackhole::initialize_remote_communication(TTDevice* tt_device) {
    // We don't want to initialize lite fabric on non-P300 boards. For all configurations we have at the moment,
    // we would need to init lite fabric just on LocalChips of P300 boards.
    // TODO: Think about future configurations where we might want to init lite fabric on other boards as well.
    if (tt_device->get_board_type() != BoardType::P300) {
        return;
    }

    auto eth_cores =
        get_soc_descriptor(tt_device).get_cores(CoreType::ETH, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0);

    std::unordered_map<uint64_t, std::vector<CoreCoord>> remote_asic_ids_to_eth_cores;

    for (const auto& eth_core : eth_cores) {
        if (!is_eth_trained(tt_device, eth_core)) {
            continue;
        }

        uint64_t remote_asic_id = get_remote_asic_id(tt_device, eth_core);
        if (chips_to_discover.find(remote_asic_id) != chips_to_discover.end()) {
            log_debug(
                LogUMD,
                "Chip {} found through ETH core {} already connected locally. Lite Fabric will not be loaded.",
                remote_asic_id,
                eth_core.str());
            continue;
        }
        remote_asic_ids_to_eth_cores[remote_asic_id].push_back(eth_core);
    }

    // TODO: be careful to not launch lite fabric on ETH cores that already have it running.
    for (const auto& [remote_asic_id, eth_cores] : remote_asic_ids_to_eth_cores) {
        // HACK Lite Fabric can be loaded only with Chip until we add necessary
        // methods to TTDevice.
        int physical_device_id = -1;
        if (tt_device->get_pci_device() != nullptr) {
            physical_device_id = tt_device->get_pci_device()->get_device_num();
        } else if (tt_device->get_jtag_device() != nullptr) {
            physical_device_id = tt_device->get_jtag_device()->get_current_device_idx().value();
        }
        std::unique_ptr<LocalChip> chip =
            LocalChip::create(physical_device_id, options.soc_descriptor_path, 0, options.io_device_type);
        lite_fabric::launch_lite_fabric(chip.get(), eth_cores);
    }
}

void TopologyDiscoveryBlackhole::init_topology_discovery() {
    int device_id = 0;
    switch (options.io_device_type) {
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

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(device_id, options.io_device_type);
    tt_device->init_tt_device();
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB_BLACKHOLE;
}

bool TopologyDiscoveryBlackhole::verify_eth_core_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) {
    static constexpr uint64_t eth_fw_major_addr = 0x7CFBE;
    static constexpr uint64_t eth_fw_minor_addr = 0x7CFBD;
    static constexpr uint64_t eth_fw_patch_addr = 0x7CFBC;
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;

    tt_device->read_from_device(&major, eth_core, eth_fw_major_addr, sizeof(uint8_t));
    tt_device->read_from_device(&minor, eth_core, eth_fw_minor_addr, sizeof(uint8_t));
    tt_device->read_from_device(&patch, eth_core, eth_fw_patch_addr, sizeof(uint8_t));
    semver_t eth_fw_version = semver_t(major, minor, patch);

    bool eth_fw_problem = false;
    if (!expected_eth_fw_version.has_value()) {
        expected_eth_fw_version =
            get_expected_eth_firmware_version_from_firmware_bundle(first_fw_bundle_version.value(), ARCH::BLACKHOLE);
        if (expected_eth_fw_version.has_value()) {
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
            "ETH FW version mismatch for chip {} ETH core {}, found: {}.",
            get_local_asic_id(tt_device, eth_core),
            eth_core.str(),
            eth_fw_version.to_string());
        eth_fw_problem = true;
    }

    // Perform this check only on local chips, as remote chips cannot do I/O without Lite Fabric,
    // which doesn't seem to work at this point.
    if (!tt_device->is_remote()) {
        tt_xy_pair translated_eth_core = get_soc_descriptor(tt_device).translate_coord_to(
            eth_core, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::NOC0, CoordSystem::TRANSLATED);
        auto hash_check = verify_eth_fw_integrity(tt_device, translated_eth_core, eth_fw_version);
        if (hash_check.has_value() && hash_check.value() == false) {
            log_warning(
                LogUMD,
                "ETH FW version hash check failed for chip {} ETH core {}",
                get_local_asic_id(tt_device, eth_core),
                eth_core.str());
            eth_fw_problem = true;
        }
    }

    return options.no_eth_firmware_strictness || !eth_fw_problem;
}

uint64_t TopologyDiscoveryBlackhole::get_unconnected_chip_id(TTDevice* tt_device) {
    uint32_t asic_id_lo = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_ID_LOW);
    uint32_t asic_id_hi = tt_device->get_arc_telemetry_reader()->read_entry(TelemetryTag::ASIC_ID_HIGH);
    return (static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo;
}

void TopologyDiscoveryBlackhole::validate_routing_firmware_state(
    const std::map<uint64_t, std::unique_ptr<TTDevice>>& devices) {}

}  // namespace tt::umd
