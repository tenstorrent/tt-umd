// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/discovery_protocol.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "tracy.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/discovery_protocol_blackhole.hpp"
#include "umd/device/topology/discovery_protocol_wormhole.hpp"
#include "umd/device/topology/topology_discovery_error.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd {

std::unique_ptr<DiscoveryProtocol> DiscoveryProtocol::create(
    const tt::ARCH arch, const TopologyDiscoveryOptions& options) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<DiscoveryProtocolWormhole>(options);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<DiscoveryProtocolBlackhole>(options);
        default:
            UMD_THROW(error::RuntimeError, fmt::format("Unsupported architecture for topology discovery."));
    }
}

void DiscoveryProtocol::add_health_error(uint64_t asic_id, ClusterDescriptor::DeviceHealthError error) {
    if (health_errors == nullptr) {
        return;
    }
    (*health_errors)[asic_id].push_back(std::move(error));
}

uint64_t DiscoveryProtocol::get_asic_id(TTDevice* tt_device) {
    // This function should return a unique ID for the device. At the moment we are going to use mangled board ID
    // and asic location from active (connected) ETH cores. If we have multiple ETH cores, we will use the first
    // one. If we have no ETH cores, we will use the board ID, since no other device can have the same board ID.
    // Using board ID should happen only for unconnected boards (N150, P150).
    const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
    std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);

    for (const CoreCoord& eth_core : eth_cores) {
        if (!is_eth_trained(tt_device, eth_core)) {
            continue;
        }

        return get_local_asic_id(tt_device, eth_core);
    }

    return get_unconnected_device_id(tt_device);
}

bool DiscoveryProtocol::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) {
    return tt_device->read_eth_core_training_status(eth_core) == EthTrainingStatus::SUCCESS;
}

void DiscoveryProtocol::wait_eth_cores_training(TTDevice* tt_device, const std::chrono::milliseconds timeout_ms) {
    ZoneScopedC(tracy::Color::DarkGreen);
    log_debug(LogUMD, "Waiting on ethernet link training on device: {}", tt_device->get_communication_device_id());
    auto timeout_left = timeout_ms;
    const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
    const std::vector<CoreCoord> eth_cores = soc_desc.get_cores(CoreType::ETH);
    for (const CoreCoord& eth_core : eth_cores) {
        tt_xy_pair actual_eth_core = soc_desc.translate_chip_coord_to_translated(eth_core, get_selected_noc_id());
        timeout_left -= tt_device->wait_eth_core_training(actual_eth_core, timeout_left);
    }
    log_debug(
        LogUMD,
        "Completed ethernet link training on device: {} after {} ms.",
        tt_device->get_communication_device_id(),
        (timeout_ms - timeout_left).count());
}

bool DiscoveryProtocol::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    SemVer eth_fw_version = get_eth_fw_version(tt_device, eth_core);

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

        SemVer minimum_supported = (get_topology_arch() == ARCH::BLACKHOLE)
                                       ? erisc_firmware::BH_MIN_ERISC_FW_SUPPORTED_VERSION
                                       : erisc_firmware::WH_MIN_ERISC_FW_SUPPORTED_VERSION;
        if (*expected_eth_fw_version < minimum_supported) {
            log_warning(
                LogUMD,
                "The expected ETH firmware version {} is older than the minimum supported version {}",
                expected_eth_fw_version->str(),
                minimum_supported.str());
            eth_fw_problem = true;
        }
    }

    if (eth_fw_version != *expected_eth_fw_version) {
        auto err = error::EthFirmwareMismatchError(
            *tt_device, asic_id, expected_eth_fw_version.value(), eth_fw_version, eth_core);
        log_warning(LogUMD, err.message());
        add_health_error(asic_id, std::move(err));
        eth_fw_problem = true;
    }

    return (options.eth_fw_mismatch_action == TopologyDiscoveryOptions::Action::IGNORE) || !eth_fw_problem;
}

bool DiscoveryProtocol::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    const auto start = std::chrono::steady_clock::now();
    uint32_t previous_reading = 0;
    // First loop: Wait until heartbeat changes from 0 (post reset).
    while (true) {
        uint32_t current_reading = get_eth_heartbeat(tt_device, eth_core);

        if (current_reading != 0) {
            previous_reading = current_reading;
            break;
        }

        if (utils::check_timeout(start, timeout::ETH_STARTUP_TIMEOUT)) {
            auto err = UMD_THROW_OR_RETURN(
                options.eth_fw_heartbeat_failure == TopologyDiscoveryOptions::Action::THROW,
                error::EthFirmwareHeartbeatError,
                *tt_device,
                asic_id,
                current_reading,
                eth_core);
            log_warning(LogUMD, err.message());
            add_health_error(asic_id, std::move(err));
            return false;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    // Second loop: Wait for heartbeat to change.
    const auto second_start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t current_reading = get_eth_heartbeat(tt_device, eth_core);
        uint32_t signature = (current_reading >> 16);

        if (signature != erisc_firmware::BASE_FW_HEARTBEAT_SIGNATURE &&
            signature != erisc_firmware::FABRIC_HEARTBEAT_SIGNATURE) {
            log_warning(
                LogUMD,
                "Read invalid heartbeat signature: {:#x} from ETH core: {}, FW possibly corrupted.",
                current_reading,
                eth_core.str());
            return false;
        }

        if (previous_reading != current_reading) {
            return true;
        }

        if (utils::check_timeout(second_start, timeout::ETH_HEARTBEAT_TIMEOUT)) {
            auto err = UMD_THROW_OR_RETURN(
                options.eth_fw_heartbeat_failure == TopologyDiscoveryOptions::Action::THROW,
                error::EthFirmwareHeartbeatError,
                *tt_device,
                asic_id,
                current_reading,
                eth_core);
            log_warning(LogUMD, err.message());
            add_health_error(asic_id, std::move(err));
            return false;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

void DiscoveryProtocol::patch_eth_connections(EthConnections& ethernet_connections, const DeviceLookup& device_lookup) {
}

}  // namespace tt::umd
