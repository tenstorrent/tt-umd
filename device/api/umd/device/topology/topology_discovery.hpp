/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <optional>

#include "umd/device/chip/chip.hpp"
#include "umd/device/chip/remote_chip.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

class ClusterDescriptor;

// TopologyDiscovery class creates cluster descriptor by discovering all chips connected to the system.
class TopologyDiscovery {
public:
    static std::unique_ptr<ClusterDescriptor> create_cluster_descriptor(
        std::unordered_set<chip_id_t> target_devices = {},
        const std::string& sdesc_path = "",
        IODeviceType io_device_type = IODeviceType::PCIe);
    TopologyDiscovery(
        std::unordered_set<chip_id_t> target_devices = {},
        const std::string& sdesc_path = "",
        IODeviceType io_device_type = IODeviceType::PCIe);
    virtual ~TopologyDiscovery() = default;
    std::unique_ptr<ClusterDescriptor> create_ethernet_map();

protected:
    void get_connected_chips();

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

    // API exposed as a temporary workaround for issue: https://tenstorrent.atlassian.net/browse/SYS-2064.
    // This is used for querying the logical remote eth channel on Multi-Host Blackhole P150 systems, where
    // we don't have access to the ethernet harvesting mask for the remote chip.
    // Logic in this API can be placed in get_remote_eth_channel, and patch_eth_connections can be removed,
    // once the issue outlined in the ticket is resolved (at which point, UMD can directly query the logical
    // ethernet channel for the remote chip on all board types).
    virtual uint32_t get_logical_remote_eth_channel(Chip* chip, tt_xy_pair local_eth_core) = 0;

    // eth_core should be in NoC 0 coordinates..
    virtual uint32_t read_port_status(Chip* chip, tt_xy_pair eth_core) = 0;

    virtual bool is_using_eth_coords() = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual std::unique_ptr<RemoteChip> create_remote_chip(
        eth_coord_t eth_coord, Chip* gateway_chip, std::set<uint32_t> gateway_eth_channels) = 0;

    Chip* get_chip(const uint64_t asic_id);

    virtual void init_topology_discovery();

    virtual bool is_eth_unconnected(Chip* chip, const tt_xy_pair eth_core) = 0;

    virtual bool is_eth_unknown(Chip* chip, const tt_xy_pair eth_core) = 0;

    // This is hack to report proper logical ETH IDs, since eth id on ETH core on Blackhole
    // does not take harvesting into consideration. This function will be overridden just for Blackhole.
    virtual void patch_eth_connections();

    // Intermesh links are ethernet links that are turned off during UMD's topology discovery but are
    // otherwise physically connected. This is done since not all tools support limiting the discovery as
    // UMD does. Once all the tools start supporting this, this feature won't be used anymore and this
    // function will return empty set.
    // This will extract the list of intermesh links from a config in L1.
    virtual std::vector<uint32_t> extract_intermesh_eth_links(Chip* chip, tt_xy_pair eth_core) = 0;

    virtual bool is_intermesh_eth_link_trained(Chip* chip, tt_xy_pair eth_core) = 0;

    std::map<uint64_t, std::unique_ptr<Chip>> chips_to_discover;
    std::map<uint64_t, std::unique_ptr<Chip>> chips;

    std::unordered_map<uint64_t, eth_coord_t> eth_coords;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>>
        ethernet_connections_to_remote_devices;

    std::unique_ptr<ClusterDescriptor> cluster_desc;

    std::unordered_set<chip_id_t> target_devices = {};

    // All board ids that should be included in the cluster descriptor.
    std::unordered_set<uint64_t> board_ids;

    std::unordered_map<uint64_t, std::set<uint32_t>> active_eth_channels_per_chip;

    const std::string sdesc_path;

    const IODeviceType io_device_type;

    bool is_running_on_6u = false;
};

}  // namespace tt::umd
