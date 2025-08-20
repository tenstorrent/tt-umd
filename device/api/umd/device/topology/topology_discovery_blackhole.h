/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/topology/topology_discovery.h"

namespace tt::umd {

class TopologyDiscoveryBlackhole : public TopologyDiscovery {
public:
    TopologyDiscoveryBlackhole(
        std::unordered_set<chip_id_t> pci_target_devices = {}, const std::string& sdesc_path = "");

protected:
    bool is_board_id_included(uint64_t board_id, uint64_t board_type) const override;

    uint64_t get_remote_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    std::optional<eth_coord_t> get_local_eth_coord(Chip* chip) override;

    std::optional<eth_coord_t> get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) override;

    tt_xy_pair get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t read_port_status(Chip* chip, tt_xy_pair eth_core) override;

    uint32_t get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) override;

    uint64_t get_remote_board_type(Chip* chip, tt_xy_pair eth_core) override;

    bool is_using_eth_coords() override;

    uint64_t mangle_asic_id(uint64_t board_id, uint8_t asic_location);

    bool is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) override;

    bool is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) override;

    std::unique_ptr<RemoteChip> create_remote_chip(Chip* gateway_chip, CoreCoord eth_core) override;

    void patch_eth_connections() override;
};

}  // namespace tt::umd
