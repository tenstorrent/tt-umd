// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "common/unsupported.hpp"

namespace tt::umd {

std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> TopologyDiscovery::discover(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    UMD_NOT_IMPLEMENTED();
}

TopologyDiscovery::TopologyDiscovery(
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
    const TopologyDiscoveryOptions& options,
    IODeviceType io_device_type) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<TopologyDiscovery> TopologyDiscovery::create_topology_discovery(
    const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path) {
    UMD_NOT_IMPLEMENTED();
}

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::create_ethernet_map() { UMD_NOT_IMPLEMENTED(); }

void TopologyDiscovery::get_connected_devices() { UMD_NOT_IMPLEMENTED(); }

void TopologyDiscovery::discover_remote_devices() { UMD_NOT_IMPLEMENTED(); }

void TopologyDiscovery::tunnel_remote_devices_over_all_gateway_links() { UMD_NOT_IMPLEMENTED(); }

std::unique_ptr<ClusterDescriptor> TopologyDiscovery::fill_cluster_descriptor_info() { UMD_NOT_IMPLEMENTED(); }

void TopologyDiscovery::wait_eth_cores_training(TTDevice* tt_device, std::chrono::milliseconds timeout_ms) {
    UMD_NOT_IMPLEMENTED();
}

bool TopologyDiscovery::is_board_id_included(uint64_t board_id) const { UMD_NOT_IMPLEMENTED(); }

bool TopologyDiscovery::eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    UMD_NOT_IMPLEMENTED();
}

uint64_t TopologyDiscovery::get_asic_id(TTDevice* tt_device) { UMD_NOT_IMPLEMENTED(); }

TTDevice* TopologyDiscovery::get_tt_device(const uint64_t asic_id) { UMD_NOT_IMPLEMENTED(); }

bool TopologyDiscovery::is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core) { UMD_NOT_IMPLEMENTED(); }

void TopologyDiscovery::patch_eth_connections() { UMD_NOT_IMPLEMENTED(); }

bool TopologyDiscovery::verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core) {
    UMD_NOT_IMPLEMENTED();
}

void TopologyDiscovery::verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id) { UMD_NOT_IMPLEMENTED(); }

bool TopologyDiscovery::init_device(TTDevice* tt_device, ChipId chip_id, std::chrono::milliseconds timeout) {
    UMD_NOT_IMPLEMENTED();
}

void TopologyDiscovery::add_local_device(std::unique_ptr<TTDevice> tt_device, int device_id) { UMD_NOT_IMPLEMENTED(); }

}  // namespace tt::umd
