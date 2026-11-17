// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "topology/topology_discovery_blackhole.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "common/unsupported.hpp"
#include "tt-umd/arc/firmware_telemetry_reader.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/telemetry.hpp"

namespace tt::umd {

namespace {

// boot_results_t holds a pointer, which is 8 bytes on the host and 4 on the ETH core, so offsetof past it would not
// match the device layout. These are the word offsets of the ETH firmware's own layout.
constexpr uint64_t LOCAL_CHIP_INFO_ADDR = blackhole::BOOT_RESULTS_ADDR + 240 * sizeof(uint32_t);
constexpr uint64_t REMOTE_CHIP_INFO_ADDR = blackhole::BOOT_RESULTS_ADDR + 248 * sizeof(uint32_t);

uint64_t asic_id_of(const blackhole::chip_info_t& chip_info) {
    return (static_cast<uint64_t>(chip_info.asic_id_hi) << 32) | chip_info.asic_id_lo;
}

}  // namespace

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    TopologyDiscovery(std::move(soc_arch_descriptor), options, io_device_type) {}

tt::ARCH TopologyDiscoveryBlackhole::get_topology_arch() const { return tt::ARCH::BLACKHOLE; }

blackhole::chip_info_t TopologyDiscoveryBlackhole::read_local_chip_info(TTDevice* tt_device, CoreCoord eth_core) {
    blackhole::chip_info_t chip_info{};
    tt_device->read_from_device(&chip_info, eth_core, LOCAL_CHIP_INFO_ADDR, sizeof(chip_info));
    return chip_info;
}

blackhole::chip_info_t TopologyDiscoveryBlackhole::read_remote_chip_info(TTDevice* tt_device, CoreCoord eth_core) {
    blackhole::chip_info_t chip_info{};
    tt_device->read_from_device(&chip_info, eth_core, REMOTE_CHIP_INFO_ADDR, sizeof(chip_info));
    return chip_info;
}

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    return read_remote_chip_info(tt_device, eth_core).get_board_id();
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    return read_local_chip_info(tt_device, eth_core).get_board_id();
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    return asic_id_of(read_local_chip_info(tt_device, eth_core));
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    return asic_id_of(read_remote_chip_info(tt_device, eth_core));
}

uint32_t TopologyDiscoveryBlackhole::get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t heartbeat = 0;
    tt_device->read_from_device(
        &heartbeat,
        eth_core,
        blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, heartbeat),
        sizeof(heartbeat));
    return heartbeat;
}

uint32_t TopologyDiscoveryBlackhole::get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t postcode = 0;
    tt_device->read_from_device(
        &postcode,
        eth_core,
        blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, postcode),
        sizeof(postcode));
    return postcode;
}

uint64_t TopologyDiscoveryBlackhole::get_unconnected_device_id(TTDevice* tt_device) {
    FirmwareTelemetryReader* telemetry_reader = tt_device->get_firmware_telemetry_reader();
    const uint64_t asic_id_high = telemetry_reader->read_entry(TelemetryTag::ASIC_ID_HIGH);
    const uint64_t asic_id_low = telemetry_reader->read_entry(TelemetryTag::ASIC_ID_LOW);
    return (asic_id_high << 32) | asic_id_low;
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    return std::nullopt;
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    return std::nullopt;
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
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
    UMD_NOT_SUPPORTED("A remote Blackhole TTDevice");
}

void TopologyDiscoveryBlackhole::init_first_device(TTDevice* tt_device) {
    // The ETH firmware the bundle ships is what every ETH core should run. Without it, the first ETH core read
    // sets the expectation instead.
    expected_eth_fw_version = tt_device->get_firmware_info_provider()->get_eth_fw_version_semver();
    is_running_on_6u = tt_device->get_board_type() == BoardType::UBB_BLACKHOLE;
}

// Blackhole ETH cores run no routing firmware, so there is no routing configuration to check.
void TopologyDiscoveryBlackhole::verify_routing_firmware_state(
    TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) {}

void TopologyDiscoveryBlackhole::patch_eth_connections() {
    for (auto& [local, remote] : ethernet_connections) {
        auto& [remote_asic_id, remote_channel] = remote;
        const SocDescriptor& remote_soc_descriptor = get_tt_device(remote_asic_id)->get_soc_descriptor();
        const CoreCoord remote_core(blackhole::ETH_CORES_NOC0.at(remote_channel), CoreType::ETH, CoordSystem::NOC0);
        remote_channel = remote_soc_descriptor.get_eth_channel_for_core(remote_core);
    }
}

// Link retraining is a Wormhole 6U recovery step; Blackhole links are not retrained by discovery.
void TopologyDiscoveryBlackhole::retrain_eth_cores() {}

}  // namespace tt::umd
