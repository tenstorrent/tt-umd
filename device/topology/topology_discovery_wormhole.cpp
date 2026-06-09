// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/topology_discovery_wormhole.hpp"

#include <fmt/format.h>

#include <memory>
#include <optional>
#include <set>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tracy.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/wormhole_eth.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {
enum class IODeviceType;

uint64_t TopologyDiscoveryWormhole::get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    if (is_running_on_6u) {
        // See comment in get_local_board_id.
        return get_remote_asic_id(tt_device, eth_core);
    }

    uint32_t board_id;
    tt_device->read_from_device(
        &board_id,
        eth_core,
        EthAddresses::RESULTS_BUF + (4 * EthAddresses::ERISC_REMOTE_BOARD_ID_LO_OFFSET),
        sizeof(uint32_t));
    return board_id;
}

uint64_t TopologyDiscoveryWormhole::get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    if (is_running_on_6u) {
        // For 6U, since the whole trays have the same board ID, and we'd want to be able to open
        // only some chips, we hack the board_id to be the asic ID. That way, the TT_VISIBLE_DEVICES filter
        // from the ClusterOptions will work correctly on 6U.
        // Note that the board_id will still be reported properly in the cluster descriptor, since it is
        // fetched through another function when cluster descriptor is being filled up.
        return get_local_asic_id(tt_device, eth_core);
    }

    // WH-ERISC mangles the ARC board id into 32 bits, just enough to be uniquely identifying.
    uint64_t board_id = tt_device->get_board_id();
    return ((board_id >> 4) & 0xF0000000) | (board_id & 0x0FFFFFFF);
}

uint64_t TopologyDiscoveryWormhole::get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t asic_id_lo;
    tt_device->read_from_device(
        &asic_id_lo,
        eth_core,
        EthAddresses::RESULTS_BUF + (4 * EthAddresses::ERISC_LOCAL_BOARD_ID_LO_OFFSET),
        sizeof(uint32_t));
    uint32_t asic_id_hi;
    tt_device->read_from_device(
        &asic_id_hi,
        eth_core,
        EthAddresses::RESULTS_BUF + (4 * (EthAddresses::ERISC_LOCAL_BOARD_ID_LO_OFFSET + 1)),
        sizeof(uint32_t));
    return ((static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo);
}

uint64_t TopologyDiscoveryWormhole::get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t asic_id_lo;
    tt_device->read_from_device(
        &asic_id_lo,
        eth_core,
        EthAddresses::RESULTS_BUF + (4 * EthAddresses::ERISC_REMOTE_BOARD_ID_LO_OFFSET),
        sizeof(uint32_t));
    uint32_t asic_id_hi;
    tt_device->read_from_device(
        &asic_id_hi,
        eth_core,
        EthAddresses::RESULTS_BUF + (4 * (EthAddresses::ERISC_REMOTE_BOARD_ID_LO_OFFSET + 1)),
        sizeof(uint32_t));
    return ((static_cast<uint64_t>(asic_id_hi) << 32) | asic_id_lo);
}

std::optional<EthCoord> TopologyDiscoveryWormhole::get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t current_device_eth_coord_info;
    tt_device->read_from_device(
        &current_device_eth_coord_info, eth_core, EthAddresses::NODE_INFO + 8, sizeof(uint32_t));

    EthCoord eth_coord;
    eth_coord.cluster_id = 0;
    eth_coord.x = (current_device_eth_coord_info >> 16) & 0xFF;
    eth_coord.y = (current_device_eth_coord_info >> 24) & 0xFF;
    eth_coord.rack = current_device_eth_coord_info & 0xFF;
    eth_coord.shelf = (current_device_eth_coord_info >> 8) & 0xFF;

    return eth_coord;
}

std::optional<EthCoord> TopologyDiscoveryWormhole::get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    const uint32_t shelf_offset = 9;
    const uint32_t rack_offset = 10;
    EthCoord eth_coord;
    eth_coord.cluster_id = 0;
    uint32_t remote_id;
    tt_device->read_from_device(&remote_id, eth_core, EthAddresses::NODE_INFO + (4 * rack_offset), sizeof(uint32_t));

    eth_coord.rack = remote_id & 0xFF;
    eth_coord.shelf = (remote_id >> 8) & 0xFF;

    tt_device->read_from_device(&remote_id, eth_core, EthAddresses::NODE_INFO + (4 * shelf_offset), sizeof(uint32_t));

    eth_coord.x = (remote_id >> 16) & 0x3F;
    eth_coord.y = (remote_id >> 22) & 0x3F;

    return eth_coord;
}

uint32_t TopologyDiscoveryWormhole::get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
    uint32_t remote_eth_id;
    tt_device->read_from_device(
        &remote_eth_id,
        local_eth_core,
        EthAddresses::RESULTS_BUF + 4 * EthAddresses::ERISC_REMOTE_ETH_ID_OFFSET,
        sizeof(uint32_t));
    return remote_eth_id;
}

uint32_t TopologyDiscoveryWormhole::get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
    return get_remote_eth_channel(tt_device, local_eth_core);
}

bool TopologyDiscoveryWormhole::is_using_eth_coords() { return !is_running_on_6u; }

void TopologyDiscoveryWormhole::init_first_device(TTDevice* tt_device) {
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB;
}

bool TopologyDiscoveryWormhole::verify_eth_core_fw_version(TTDevice* tt_device, CoreCoord eth_core) {
    SemVer eth_fw_version = get_eth_fw_version(tt_device, eth_core);
    uint64_t current_device_asic_id = get_asic_id(tt_device);

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
            "ETH FW version mismatch for device ASIC ID: {} ETH core {}, expected: {}, got {}.",
            current_device_asic_id,
            eth_core.str(),
            expected_eth_fw_version->to_string(),
            eth_fw_version.to_string());
        eth_fw_problem = true;
    }

    return (options.eth_fw_mismatch_action == TopologyDiscoveryOptions::Action::IGNORE) || !eth_fw_problem;
}

uint64_t TopologyDiscoveryWormhole::get_unconnected_device_id(TTDevice* tt_device) { return tt_device->get_board_id(); }

void TopologyDiscoveryWormhole::verify_routing_firmware_state(TTDevice* tt_device, const CoreCoord eth_core) {
    uint32_t routing_firmware_disabled;
    tt_device->read_from_device(
        &routing_firmware_disabled, eth_core, EthAddresses::ROUTING_FIRMWARE_STATE, sizeof(uint32_t));
    if (is_running_on_6u && routing_firmware_disabled == 0) {
        auto message = fmt::format(
            "Routing FW on 6U unexpectedly enabled on device {} core {}.",
            get_local_asic_id(tt_device, eth_core),
            eth_core.str());
        if (options.unexpected_routing_firmware_config == TopologyDiscoveryOptions::Action::IGNORE) {
            log_warning(LogUMD, message);
            return;
        }
        UMD_THROW(error::RuntimeError, message);
    } else if (!is_running_on_6u && routing_firmware_disabled == 1) {
        auto message = fmt::format(
            "Routing FW unexpectedly disabled on device {} core {}.",
            get_local_asic_id(tt_device, eth_core),
            eth_core.str());
        if (options.unexpected_routing_firmware_config == TopologyDiscoveryOptions::Action::IGNORE) {
            log_warning(LogUMD, message);
            return;
        }
        UMD_THROW(error::RuntimeError, message);
    }
}

bool TopologyDiscoveryWormhole::is_eth_port_disabled(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t port_disable_mask = 0;
    tt_device->read_from_device(
        &port_disable_mask, eth_core, wormhole::ETH_BOOT_PARAMS_PORT_DISABLE_ADDR, sizeof(uint32_t));
    const uint32_t channel = tt_device->get_soc_descriptor().get_eth_channel_for_core(eth_core);
    return (port_disable_mask >> channel) & 1;
}

uint32_t TopologyDiscoveryWormhole::get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t heartbeat_value = 0;
    tt_device->read_from_device(&heartbeat_value, eth_core, wormhole::ETH_HEARTBEAT_ADDR, sizeof(uint32_t));
    return heartbeat_value;
}

uint32_t TopologyDiscoveryWormhole::get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t postcode = 0;
    tt_device->read_from_device(&postcode, eth_core, wormhole::ETH_POSTCODE_ADDR, sizeof(uint32_t));
    return postcode;
}

void TopologyDiscoveryWormhole::retrain_eth_cores() {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (!is_running_on_6u || !options.perform_6u_eth_retrain) {
        return;
    }

    for (uint32_t attempt = 0; attempt < ETH_RETRAIN_ATTEMPT_COUNT; attempt++) {
        log_debug(LogUMD, "Retraining ETH cores on Wormhole B0 devices, iteration {}.", attempt + 1);
        bool all_eth_cores_trained = true;

        for (const auto& [asic_id, tt_device] : devices_to_discover) {
            for (const CoreCoord& eth_core : tt_device->get_soc_descriptor().get_cores(CoreType::ETH)) {
                xy_pair translated_eth_core =
                    tt_device->get_soc_descriptor().translate_chip_coord_to_translated(eth_core);
                EthTrainingStatus status = tt_device->read_eth_core_training_status(translated_eth_core);
                bool should_retrain = (status == EthTrainingStatus::FAIL) ||
                                      (RETRAIN_UNCONNECTED && status == EthTrainingStatus::NOT_CONNECTED);
                if (!should_retrain) {
                    continue;
                }

                SemVer eth_fw_version = get_eth_fw_version(tt_device.get(), eth_core);
                if (eth_fw_version < wormhole::MIN_ETH_FW_VERSION_FOR_RETRAIN) {
                    log_warning(
                        LogUMD,
                        "ETH FW version {} is older than minimum version needed for retraining {}. Skipping retrain.",
                        eth_fw_version.to_string(),
                        wormhole::MIN_ETH_FW_VERSION_FOR_RETRAIN.to_string());
                    return;
                }

                log_debug(
                    LogUMD, "Retraining ETH core {} on device {}, attempt {}.", eth_core.str(), asic_id, attempt + 1);
                uint32_t trigger_val = wormhole::ETH_TRIGGER_RETRAIN_VAL;
                tt_device->write_to_device(
                    &trigger_val, translated_eth_core, wormhole::ETH_RETRAIN_ADDR, sizeof(uint32_t));
                all_eth_cores_trained = false;
            }
        }

        if (all_eth_cores_trained) {
            break;
        }

        for (const auto& [asic_id, tt_device] : devices_to_discover) {
            log_debug(LogUMD, "Waiting for ETH cores to finish training after retrain on device {}.", asic_id);
            wait_eth_cores_training(tt_device.get());
        }
    }
}
}  // namespace tt::umd
