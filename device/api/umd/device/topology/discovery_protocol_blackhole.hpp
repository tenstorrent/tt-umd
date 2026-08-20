// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "umd/device/topology/discovery_protocol.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {
struct TopologyDiscoveryOptions;

class DiscoveryProtocolBlackhole : public DiscoveryProtocol {
public:
    explicit DiscoveryProtocolBlackhole(const TopologyDiscoveryOptions& options) : DiscoveryProtocol(options) {}

    tt::ARCH get_topology_arch() const override { return tt::ARCH::BLACKHOLE; }

    uint64_t get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_unconnected_device_id(TTDevice* tt_device) override;

    std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) override;

    std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) override;

    uint32_t get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) override;

    uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) override;

    bool is_using_eth_coords() override;

    void verify_routing_firmware_state(TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) override {}

    std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord,
        TTDevice* gateway_device,
        std::set<uint32_t> gateway_eth_channels,
        const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor) override;

    void patch_eth_connections(EthConnections& ethernet_connections, const DeviceLookup& device_lookup) override;

    void init_first_device(TTDevice* tt_device) override;

    uint32_t get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) override;

    uint32_t get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) override;

    void retrain_eth_cores(const std::map<uint64_t, std::unique_ptr<TTDevice>>& devices) override;

private:
    uint64_t mangle_asic_id(uint64_t board_id, uint8_t asic_location);
};

}  // namespace tt::umd
