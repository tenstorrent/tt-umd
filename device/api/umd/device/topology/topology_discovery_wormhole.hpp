/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/topology/topology_discovery.hpp"

namespace tt::umd {

class TopologyDiscoveryWormhole : public TopologyDiscovery {
public:
    TopologyDiscoveryWormhole(const TopologyDiscoveryOptions& options);

protected:
    struct EthAddresses {
        uint32_t masked_version;

        uint64_t eth_param_table;
        uint64_t routing_firmware_state;
        uint64_t node_info;
        uint64_t eth_conn_info;
        uint64_t results_buf;
        uint64_t erisc_remote_board_type_offset;
        uint64_t erisc_local_board_type_offset;
        uint64_t erisc_local_board_id_lo_offset;
        uint64_t erisc_remote_board_id_lo_offset;
        uint64_t erisc_remote_eth_id_offset;
    };

    bool is_board_id_included(uint64_t board_id, uint64_t board_type) const override;

    static EthAddresses get_eth_addresses(uint32_t eth_fw_version);

    uint64_t get_remote_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_board_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_local_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) override;

    uint64_t get_unconnected_chip_id(Chip* chip) override;

    std::optional<EthCoord> get_local_eth_coord(Chip* chip, tt_xy_pair eth_core) override;

    std::optional<EthCoord> get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) override;

    tt_xy_pair get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t read_training_status(Chip* chip, tt_xy_pair eth_core);

    uint32_t get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) override;

    uint32_t get_logical_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) override;

    uint64_t get_remote_board_type(Chip* chip, tt_xy_pair eth_core) override;

    std::unique_ptr<RemoteChip> create_remote_chip(
        std::optional<EthCoord> eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) override;

    bool is_using_eth_coords() override;

    void init_topology_discovery() override;

    bool is_eth_trained(Chip* chip, const tt_xy_pair eth_core) override;

    void validate_routing_firmware_state(const std::map<uint64_t, std::unique_ptr<Chip>>& chips) override;

    EthAddresses eth_addresses;

    bool verify_eth_core_fw_version(Chip* chip, CoreCoord eth_core) override;

    std::optional<semver_t> get_expected_erisc_fw_version_from_fw_bundle(semver_t fw_bundle_version) const override;

    static constexpr uint32_t LINK_TRAIN_SUCCESS = 1;
    static constexpr uint32_t LINK_TRAIN_TRAINING = 0;
};
}  // namespace tt::umd
