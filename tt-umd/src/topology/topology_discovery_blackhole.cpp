// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "topology/topology_discovery_blackhole.hpp"

#include <cstddef>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// The ERISC firmware publishes what it knows about this chip and about its link partner in its boot results.
blackhole::chip_info_t read_chip_info(TTDevice* tt_device, CoreCoord eth_core, size_t offset) {
    blackhole::chip_info_t chip_info = {};
    tt_device->read_from_device(&chip_info, eth_core, blackhole::BOOT_RESULTS_ADDR + offset, sizeof(chip_info));
    return chip_info;
}

blackhole::chip_info_t read_local_chip_info(TTDevice* tt_device, CoreCoord eth_core) {
    return read_chip_info(tt_device, eth_core, offsetof(blackhole::boot_results_t, local_info));
}

blackhole::chip_info_t read_remote_chip_info(TTDevice* tt_device, CoreCoord eth_core) {
    return read_chip_info(tt_device, eth_core, offsetof(blackhole::boot_results_t, remote_info));
}

uint32_t read_eth_status_word(TTDevice* tt_device, CoreCoord eth_core, size_t offset) {
    uint32_t value = 0;
    tt_device->read_from_device(&value, eth_core, blackhole::BOOT_RESULTS_ADDR + offset, sizeof(value));
    return value;
}

uint64_t to_asic_id(const blackhole::chip_info_t& chip_info) {
    return (static_cast<uint64_t>(chip_info.asic_id_hi) << 32) | chip_info.asic_id_lo;
}

}  // namespace

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    TopologyDiscovery(std::move(soc_arch_descriptor), options, io_device_type) {}

tt::ARCH TopologyDiscoveryBlackhole::get_topology_arch() const { return tt::ARCH::BLACKHOLE; }

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    return read_remote_chip_info(tt_device, eth_core).get_board_id();
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    return read_local_chip_info(tt_device, eth_core).get_board_id();
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    return to_asic_id(read_local_chip_info(tt_device, eth_core));
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    return to_asic_id(read_remote_chip_info(tt_device, eth_core));
}

bool TopologyDiscoveryBlackhole::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    // CMFW zeroes ETH L1 before loading the firmware, which starts beating only once link training has ended. A zero
    // word is firmware that has not started yet, not a crash: the core cannot serve I/O, but there is nothing to
    // report.
    if (get_eth_heartbeat(tt_device, eth_core) == 0) {
        log_debug(LogUMD, "ETH firmware on core {} has not started its heartbeat yet.", eth_core.str());
        return false;
    }
    return TopologyDiscovery::eth_heartbeat_running(tt_device, asic_id, eth_core);
}

uint32_t TopologyDiscoveryBlackhole::get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) {
    return read_eth_status_word(tt_device, eth_core, offsetof(blackhole::eth_status_t, heartbeat));
}

uint32_t TopologyDiscoveryBlackhole::get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) {
    return read_eth_status_word(tt_device, eth_core, offsetof(blackhole::eth_status_t, postcode));
}

uint64_t TopologyDiscoveryBlackhole::get_unconnected_device_id(TTDevice* tt_device) {
    // The fused ASIC id, the same value the ERISC firmware hands to link partners.
    FirmwareTelemetryReader* telemetry = tt_device->get_firmware_telemetry_reader();
    return (static_cast<uint64_t>(telemetry->read_entry(TelemetryTag::ASIC_ID_HIGH)) << 32) |
           telemetry->read_entry(TelemetryTag::ASIC_ID_LOW);
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    return std::nullopt;
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    return std::nullopt;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
    // The partner's physical ETH instance; patch_eth_connections turns it into a logical channel.
    return read_remote_chip_info(tt_device, local_eth_core).eth_id;
}

uint32_t TopologyDiscoveryBlackhole::get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
    return read_remote_chip_info(tt_device, local_eth_core).logical_eth_id;
}

bool TopologyDiscoveryBlackhole::is_using_eth_coords() { return false; }

std::unique_ptr<TTDevice> TopologyDiscoveryBlackhole::create_remote_device(
    std::optional<EthCoord> eth_coord,
    TTDevice* gateway_device,
    std::set<uint32_t> gateway_eth_channels,
    const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor) {
    UMD_THROW(error::RuntimeError, "Remote Blackhole devices are not supported.");
}

void TopologyDiscoveryBlackhole::init_first_device(TTDevice* tt_device) {
    // Nothing to configure: the expected ETH FW version comes from the link walk, which checks heartbeats first.
}

void TopologyDiscoveryBlackhole::verify_routing_firmware_state(
    TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) {
    // Blackhole ETH cores run no routing firmware.
}

void TopologyDiscoveryBlackhole::patch_eth_connections() {
    // The firmware reports the partner's physical ETH instance, which counts harvested instances too.
    for (auto& [local, remote] : ethernet_connections) {
        const SocDescriptor& remote_soc_descriptor = get_tt_device(remote.first)->get_soc_descriptor();
        const CoreCoord remote_core(blackhole::ETH_CORES_NOC0.at(remote.second), CoreType::ETH, CoordSystem::NOC0);
        remote.second = remote_soc_descriptor.get_eth_channel_for_core(remote_core, CoordSystem::NOC0);
    }
}

// Retraining is a 6U Wormhole galaxy recovery; Blackhole links are not retrained by discovery.
void TopologyDiscoveryBlackhole::retrain_eth_cores() {}

}  // namespace tt::umd
