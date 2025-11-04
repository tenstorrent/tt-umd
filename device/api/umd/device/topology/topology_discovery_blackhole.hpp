/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/topology/topology_discovery.hpp"

namespace tt::umd {

class TopologyDiscoveryBlackhole : public TopologyDiscovery {
public:
    TopologyDiscoveryBlackhole(const TopologyDiscoveryOptions& options);

protected:
    bool is_board_id_included(uint64_t board_id, uint64_t board_type) const override;

    uint64_t get_remote_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_unconnected_chip_id(Chip* chip) override;

    std::optional<EthCoord> get_local_eth_coord(Chip* chip) override;

    std::optional<EthCoord> get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) override;

    tt_xy_pair get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t read_port_status(Chip* chip, tt_xy_pair eth_core);

    uint32_t get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t get_logical_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) override;

    uint64_t get_remote_board_type(Chip* chip, tt_xy_pair eth_core) override;

    bool is_using_eth_coords() override;

    uint64_t mangle_asic_id(uint64_t board_id, uint8_t asic_location);

    bool is_eth_trained(Chip* chip, const tt_xy_pair eth_core) override;

    void validate_routing_firmware_state(const std::map<uint64_t, std::unique_ptr<Chip>>& chips) override;

    std::unique_ptr<RemoteChip> create_remote_chip(
        std::optional<EthCoord> eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) override;

    void patch_eth_connections() override;

    void initialize_remote_communication(Chip* chip) override;

    void init_topology_discovery() override;

    bool verify_eth_core_fw_version(Chip* chip, CoreCoord eth_core) override;
};

}  // namespace tt::umd
