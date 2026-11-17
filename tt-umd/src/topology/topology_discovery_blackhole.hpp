// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>

#include "tt-umd/topology/topology_discovery.hpp"

namespace tt::umd {

/**
 * @brief Topology discovery for Blackhole clusters.
 */
class TopologyDiscoveryBlackhole : public TopologyDiscovery {
public:
    TopologyDiscoveryBlackhole(
        std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe);

protected:
    tt::ARCH get_topology_arch() const override;

    uint64_t get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) override;
    uint64_t get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) override;
    uint64_t get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) override;
    uint64_t get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) override;
    uint32_t get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) override;
    uint32_t get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) override;
    uint64_t get_unconnected_device_id(TTDevice* tt_device) override;
    std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) override;
    std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) override;
    uint32_t get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) override;
    uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) override;
    bool is_using_eth_coords() override;
    std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord,
        TTDevice* gateway_device,
        std::set<uint32_t> gateway_eth_channels,
        const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor = nullptr) override;
    void init_first_device(TTDevice* tt_device) override;
    void verify_routing_firmware_state(TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) override;
    void patch_eth_connections() override;
    void retrain_eth_cores() override;
};

}  // namespace tt::umd
