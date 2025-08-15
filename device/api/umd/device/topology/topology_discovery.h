/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <optional>

#include "umd/device/chip/chip.h"
#include "umd/device/chip/remote_chip.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

class tt_ClusterDescriptor;

// TopologyDiscovery class creates cluster descriptor by discovering all chips connected to the system.
class TopologyDiscovery {
public:
    static std::unique_ptr<tt_ClusterDescriptor> create_cluster_descriptor(
        std::unordered_set<chip_id_t> pci_target_devices = {}, const std::string& sdesc_path = "");
    TopologyDiscovery(std::unordered_set<chip_id_t> pci_target_devices = {}, const std::string& sdesc_path = "");
    virtual ~TopologyDiscovery() = default;
    std::unique_ptr<tt_ClusterDescriptor> create_ethernet_map();

protected:
    void get_pcie_connected_chips();

    void discover_remote_chips();

    void fill_cluster_descriptor_info();

    // board_type is not used for all configs.
    // We need to know that we are seeing TG board and that we should include it in the topology.
    virtual bool is_board_id_included(uint64_t board_id, uint64_t board_type) const = 0;

    // Returns mangled remote board id from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_remote_board_id(Chip* chip, tt_xy_pair eth_core) = 0;

    // Returns mangled remote board type from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_remote_board_type(Chip* chip, tt_xy_pair eth_core) = 0;

    // Returns mangled local board id from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_local_board_id(Chip* chip, tt_xy_pair eth_core) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual uint64_t get_local_asic_id(Chip* chip, tt_xy_pair eth_core) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual uint64_t get_remote_asic_id(Chip* chip, tt_xy_pair eth_core) = 0;

    uint64_t get_asic_id(Chip* chip);

    virtual std::optional<eth_coord_t> get_local_eth_coord(Chip* chip) = 0;

    virtual std::optional<eth_coord_t> get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core) = 0;

    // local_eth_core should be in NoC 0 coordinates.
    virtual tt_xy_pair get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core) = 0;

    // local_eth_core should be in NoC 0 coordinates.
    virtual uint32_t get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core) = 0;

    virtual uint32_t get_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) = 0;

    // eth_core should be in NoC 0 coordinates..
    virtual uint32_t read_port_status(Chip* chip, tt_xy_pair eth_core) = 0;

    virtual bool is_using_eth_coords() = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual std::unique_ptr<RemoteChip> create_remote_chip(
        Chip* chip, tt_xy_pair eth_core, Chip* gateway_chip, std::set<uint32_t>& eth_channels_to_use) = 0;

    Chip* get_chip(const uint64_t asic_id);

    virtual void init_topology_discovery();

    virtual bool is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) = 0;

    virtual bool is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) = 0;

    std::map<uint64_t, std::unique_ptr<Chip>> chips_to_discover;
    std::map<uint64_t, std::unique_ptr<Chip>> chips;

    std::unordered_map<uint64_t, eth_coord_t> eth_coords;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>>
        ethernet_connections_to_remote_devices;

    std::unique_ptr<tt_ClusterDescriptor> cluster_desc;

    std::unordered_set<chip_id_t> pci_target_devices = {};

    // All board ids that should be included in the cluster descriptor.
    std::unordered_set<uint64_t> board_ids;

    std::unordered_map<uint64_t, std::set<uint32_t>> active_eth_channels_per_chip;

    const std::string sdesc_path;
};

}  // namespace tt::umd
