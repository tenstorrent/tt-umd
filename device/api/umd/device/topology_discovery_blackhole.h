/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/topology_discovery.h"

namespace tt::umd {

class TopologyDiscoveryBlackhole : public TopologyDiscovery {
public:
    TopologyDiscoveryBlackhole(
        std::unordered_set<chip_id_t> pci_target_devices = {}, const std::string& sdesc_path = "");

protected:
    uint64_t get_remote_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_asic_id(Chip* chip) override;

    eth_coord_t get_local_eth_coord(Chip* chip) override;

    eth_coord_t get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) override;

    tt_xy_pair get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t read_port_status(Chip* chip, tt_xy_pair eth_core, uint32_t channel) override;

    std::unique_ptr<RemoteChip> create_remote_chip(Chip* chip, tt_xy_pair eth_core, Chip* gateway_chip) override;
};

}  // namespace tt::umd
