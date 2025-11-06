/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TopologyDiscoveryBlackhole : public TopologyDiscovery {
public:
    TopologyDiscoveryBlackhole(const TopologyDiscoveryOptions& options);

protected:
    bool is_board_id_included(uint64_t board_id, uint64_t board_type) const override;

    uint64_t get_remote_board_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_local_board_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_local_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_remote_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_unconnected_chip_id(TTDevice* tt_device) override;

    std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) override;

    std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) override;

    tt_xy_pair get_remote_eth_core(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint32_t read_port_status(TTDevice* tt_device, tt_xy_pair eth_core);

    uint32_t get_remote_eth_id(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint32_t get_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint64_t get_remote_board_type(TTDevice* tt_device, tt_xy_pair eth_core) override;

    bool is_using_eth_coords() override;

    uint64_t mangle_asic_id(uint64_t board_id, uint8_t asic_location);

    bool is_eth_trained(TTDevice* tt_device, const tt_xy_pair eth_core) override;

    void validate_routing_firmware_state(const std::map<uint64_t, std::unique_ptr<TTDevice>>& devices) override;

    std::unique_ptr<TTDevice> create_remote_chip(
        std::optional<EthCoord> eth_coord, TTDevice* gateway_chip, std::set<uint32_t> gateway_eth_channels) override;

    void patch_eth_connections() override;

    void initialize_remote_communication(TTDevice* tt_device) override;

    void init_topology_discovery() override;

    bool verify_eth_core_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) override;
};

}  // namespace tt::umd
