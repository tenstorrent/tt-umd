// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "topology/topology_discovery_blackhole.hpp"

#include <utility>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

TopologyDiscoveryBlackhole::TopologyDiscoveryBlackhole(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    TopologyDiscovery(std::move(soc_arch_descriptor), options, io_device_type) {}

tt::ARCH TopologyDiscoveryBlackhole::get_topology_arch() const { return tt::ARCH::BLACKHOLE; }

uint64_t TopologyDiscoveryBlackhole::get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_remote_board_id is not implemented yet");
}

uint64_t TopologyDiscoveryBlackhole::get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_local_board_id is not implemented yet");
}

uint64_t TopologyDiscoveryBlackhole::get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_local_asic_id is not implemented yet");
}

uint64_t TopologyDiscoveryBlackhole::get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_remote_asic_id is not implemented yet");
}

bool TopologyDiscoveryBlackhole::is_eth_port_disabled(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::is_eth_port_disabled is not implemented yet");
}

uint32_t TopologyDiscoveryBlackhole::get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_eth_heartbeat is not implemented yet");
}

uint32_t TopologyDiscoveryBlackhole::get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_eth_postcode is not implemented yet");
}

uint64_t TopologyDiscoveryBlackhole::get_unconnected_device_id(TTDevice* tt_device) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_unconnected_device_id is not implemented yet");
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_local_eth_coord is not implemented yet");
}

std::optional<EthCoord> TopologyDiscoveryBlackhole::get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_remote_eth_coord is not implemented yet");
}

uint32_t TopologyDiscoveryBlackhole::get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_remote_eth_channel is not implemented yet");
}

uint32_t TopologyDiscoveryBlackhole::get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::get_logical_remote_eth_channel is not implemented yet");
}

bool TopologyDiscoveryBlackhole::is_using_eth_coords() {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::is_using_eth_coords is not implemented yet");
}

std::unique_ptr<TTDevice> TopologyDiscoveryBlackhole::create_remote_device(
    std::optional<EthCoord> eth_coord,
    TTDevice* gateway_device,
    std::set<uint32_t> gateway_eth_channels,
    const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::create_remote_device is not implemented yet");
}

void TopologyDiscoveryBlackhole::init_first_device(TTDevice* tt_device) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::init_first_device is not implemented yet");
}

void TopologyDiscoveryBlackhole::verify_routing_firmware_state(
    TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::verify_routing_firmware_state is not implemented yet");
}

void TopologyDiscoveryBlackhole::patch_eth_connections() {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::patch_eth_connections is not implemented yet");
}

void TopologyDiscoveryBlackhole::retrain_eth_cores() {
    UMD_THROW(error::RuntimeError, "TopologyDiscoveryBlackhole::retrain_eth_cores is not implemented yet");
}

}  // namespace tt::umd
