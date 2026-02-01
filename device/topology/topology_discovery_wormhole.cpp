// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/topology_discovery_wormhole.hpp"

#include <cstdint>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "assert.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"
#include "wormhole/eth_l1_address_map.h"

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

uint64_t TopologyDiscoveryWormhole::get_remote_board_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // See comment in get_local_board_id.
        return get_remote_asic_id(tt_device, eth_core);
    }

    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_id_lo_offset),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscoveryWormhole::get_local_board_id(TTDevice* tt_device, tt_xy_pair eth_core) {
    if (is_running_on_6u) {
        // For 6U, since the whole trays have the same board ID, and we'd want to be able to open
        // only some chips, we hack the board_id to be the asic ID. That way, the pci_target_devices filter
        // from the ClusterOptions will work correctly on 6U.
        // Note that the board_id will still be reported properly in the cluster descriptor, since it is
        // fetched through another function when cluster descriptor is being filled up.
        return get_local_asic_id(tt_device, eth_core);
    }

    // WH-ERISC mangles the ARC board id into 32 bits, just enough to be uniquely identifying.
    uint64_t board_id = tt_device->get_board_id();
    return ((board_id >> 4) & 0xF0000000) | (board_id & 0x0FFFFFFF);
}

uint64_t TopologyDiscoveryWormhole::get_remote_board_type(TTDevice* tt_device, tt_xy_pair eth_core) {
    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        eth_addresses.results_buf + (4 * eth_addresses.erisc_remote_board_type_offset),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscoveryWormhole::get_local_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) {
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

uint64_t TopologyDiscoveryWormhole::get_remote_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) {
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

tt_xy_pair TopologyDiscoveryWormhole::get_remote_eth_core(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    const uint32_t shelf_offset = 9;
    uint32_t remote_id;
    tt_device->read_from_device(
        &remote_id,
        {local_eth_core.x, local_eth_core.y},
        eth_addresses.node_info + (4 * shelf_offset),
        sizeof(uint32_t));

    return tt_xy_pair{(remote_id >> 4) & 0x3F, (remote_id >> 10) & 0x3F};
}

uint32_t TopologyDiscoveryWormhole::read_training_status(TTDevice* tt_device, tt_xy_pair eth_core) {
    uint32_t training_status;
    tt_device->read_from_device(&training_status, eth_core, 0x1104, sizeof(uint32_t));
    return training_status;
}

uint32_t TopologyDiscoveryWormhole::get_remote_eth_id(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    if (!is_running_on_6u) {
        throw std::runtime_error(
            "get_remote_eth_id should not be called on non-6U configurations. This message likely indicates a bug.");
    }
    uint32_t remote_eth_id;
    tt_device->read_from_device(
        &remote_eth_id,
        local_eth_core,
        eth_addresses.results_buf + 4 * eth_addresses.erisc_remote_eth_id_offset,
        sizeof(uint32_t));
    return remote_eth_id;
}

std::optional<EthCoord> TopologyDiscoveryWormhole::get_local_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) {
    uint32_t current_device_eth_coord_info;
    tt_device->read_from_device(
        &current_device_eth_coord_info, eth_core, eth_addresses.node_info + 8, sizeof(uint32_t));

    EthCoord eth_coord;
    eth_coord.cluster_id = 0;
    eth_coord.x = (current_device_eth_coord_info >> 16) & 0xFF;
    eth_coord.y = (current_device_eth_coord_info >> 24) & 0xFF;
    eth_coord.rack = current_device_eth_coord_info & 0xFF;
    eth_coord.shelf = (current_device_eth_coord_info >> 8) & 0xFF;

    return eth_coord;
}

std::optional<EthCoord> TopologyDiscoveryWormhole::get_remote_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) {
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;
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

std::unique_ptr<TTDevice> TopologyDiscoveryWormhole::create_remote_device(
    std::optional<EthCoord> eth_coord, TTDevice* gateway_device, std::set<uint32_t> gateway_eth_channels) {
    if (is_running_on_6u) {
        return nullptr;
    }
    EthCoord remote_device_eth_coord = eth_coord.has_value() ? eth_coord.value() : EthCoord{0, 0, 0, 0};

    std::unique_ptr<RemoteCommunication> remote_communication =
        RemoteCommunication::create_remote_communication(gateway_device, remote_device_eth_coord);
    remote_communication->set_remote_transfer_ethernet_cores(
        get_soc_descriptor(gateway_device)
            .get_eth_xy_pairs_for_channels(gateway_eth_channels, CoordSystem::TRANSLATED));
    std::unique_ptr<TTDevice> remote_tt_device = TTDevice::create(std::move(remote_communication));
    remote_tt_device->init_tt_device();
    return remote_tt_device;
}

uint32_t TopologyDiscoveryWormhole::get_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    if (is_running_on_6u) {
        return get_remote_eth_id(tt_device, local_eth_core);
    }
    tt_xy_pair remote_eth_core = get_remote_eth_core(tt_device, local_eth_core);

    // TODO(pjanevski): explain in comment why we are using chip instead of remote chip.
    return get_soc_descriptor(tt_device).translate_coord_to(remote_eth_core, CoordSystem::NOC0, CoordSystem::LOGICAL).y;
}

uint32_t TopologyDiscoveryWormhole::get_logical_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) {
    return get_remote_eth_channel(tt_device, local_eth_core);
}

bool TopologyDiscoveryWormhole::is_using_eth_coords() { return !is_running_on_6u; }

void TopologyDiscoveryWormhole::init_first_device(TTDevice* tt_device) {
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

bool TopologyDiscoveryWormhole::is_eth_trained(TTDevice* tt_device, const tt_xy_pair eth_core) {
    return read_training_status(tt_device, eth_core) == LINK_TRAIN_SUCCESS;
}

bool TopologyDiscoveryWormhole::verify_eth_core_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) {
    uint32_t eth_fw_version_read;
    tt_device->read_from_device(
        &eth_fw_version_read, eth_core, eth_l1_mem::address_map::FW_VERSION_ADDR, sizeof(uint32_t));

    semver_t eth_fw_version = semver_t::from_wormhole_eth_firmware_tag(eth_fw_version_read);

    bool eth_fw_problem = false;
    if (!expected_eth_fw_version.has_value()) {
        expected_eth_fw_version = tt_device->get_firmware_info_provider()->get_eth_fw_version_semver();
        if (expected_eth_fw_version.has_value()) {
            log_debug(LogUMD, "Expected ETH FW version from telemetry: {}", expected_eth_fw_version->to_string());
        } else {
            expected_eth_fw_version = eth_fw_version;
            log_debug(
                LogUMD, "Established ETH FW version from first discovered ETH core: {}", eth_fw_version.to_string());
        }
        if (erisc_firmware::WH_MIN_ERISC_FW_SUPPORTED_VERSION > eth_fw_version) {
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

    if (options.verify_eth_fw_hash) {
        auto hash_check = verify_eth_fw_integrity(tt_device, eth_core, eth_fw_version);
        if (hash_check.has_value() && !hash_check.value()) {
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

uint64_t TopologyDiscoveryWormhole::get_unconnected_device_id(TTDevice* tt_device) { return tt_device->get_board_id(); }

bool TopologyDiscoveryWormhole::verify_routing_firmware_state(TTDevice* tt_device, const tt_xy_pair eth_core) {
    uint32_t routing_firmware_disabled;
    tt_device->read_from_device(
        &routing_firmware_disabled, eth_core, eth_addresses.routing_firmware_state, sizeof(uint32_t));
    if (is_running_on_6u && routing_firmware_disabled == 0) {
        auto message = fmt::format(
            "Routing FW on 6U unexpectedly enabled on device {} core {}.",
            get_local_asic_id(tt_device, eth_core),
            eth_core.str());
        if (options.no_eth_firmware_strictness) {
            log_warning(LogUMD, message);
            return false;
        }
        TT_THROW(message);
    } else if (!is_running_on_6u && routing_firmware_disabled == 1) {
        auto message = fmt::format(
            "Routing FW unexpectedly disabled on device {} core {}.",
            get_local_asic_id(tt_device, eth_core),
            eth_core.str());
        if (options.no_eth_firmware_strictness) {
            log_warning(LogUMD, message);
            return false;
        }
        TT_THROW(message);
    }
    return true;
}

}  // namespace tt::umd
