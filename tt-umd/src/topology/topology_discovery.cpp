// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery.hpp"

#include <utility>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

TopologyDiscovery::TopologyDiscovery(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) :
    options(options), io_device_type(io_device_type), soc_arch_descriptor_(std::move(soc_arch_descriptor)) {}

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::discover is not implemented yet");
}

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::create_topology_discovery is not implemented yet");
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::create_ethernet_map is not implemented yet");
}

void TopologyDiscovery::get_connected_devices() {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::get_connected_devices is not implemented yet");
}

void TopologyDiscovery::discover_remote_devices() {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::discover_remote_devices is not implemented yet");
}

void TopologyDiscovery::tunnel_remote_devices_over_all_gateway_links() {
    UMD_THROW(
        error::RuntimeError, "TopologyDiscovery::tunnel_remote_devices_over_all_gateway_links is not implemented yet");
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::fill_cluster_descriptor_info() {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::fill_cluster_descriptor_info is not implemented yet");
}

void TopologyDiscovery::wait_eth_cores_training(TTDevice* tt_device, std::chrono::milliseconds timeout_ms) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::wait_eth_cores_training is not implemented yet");
}

bool TopologyDiscovery::is_board_id_included(uint64_t board_id) const {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::is_board_id_included is not implemented yet");
}

bool TopologyDiscovery::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::eth_heartbeat_running is not implemented yet");
}

uint64_t TopologyDiscovery::get_asic_id(TTDevice* tt_device) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::get_asic_id is not implemented yet");
}

TTDevice* TopologyDiscovery::get_tt_device(const uint64_t asic_id) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::get_tt_device is not implemented yet");
}

bool TopologyDiscovery::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::is_eth_trained is not implemented yet");
}

void TopologyDiscovery::patch_eth_connections() {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::patch_eth_connections is not implemented yet");
}

bool TopologyDiscovery::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::verify_eth_core_fw_version is not implemented yet");
}

void TopologyDiscovery::verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::verify_fw_bundle_version is not implemented yet");
}

bool TopologyDiscovery::init_device(TTDevice* tt_device, ChipId chip_id, std::chrono::milliseconds timeout) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::init_device is not implemented yet");
}

void TopologyDiscovery::add_local_device(std::unique_ptr<TTDevice> tt_device, int device_id) {
    UMD_THROW(error::RuntimeError, "TopologyDiscovery::add_local_device is not implemented yet");
}

}  // namespace tt::umd
