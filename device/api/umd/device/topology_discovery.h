/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/chip/remote_chip.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"
#include "umd/device/tt_device/tt_device.h"

class tt_ClusterDescriptor;

namespace tt::umd {

// TopologyDiscovery class creates cluster descriptor only for Wormhole configurations with old routing fw.
// TODO: Move Blackhole topology discovery to this class.
class TopologyDiscovery {
public:
    TopologyDiscovery(std::unordered_set<chip_id_t> pci_target_devices = {}, const std::string& sdesc_path = "");
    std::unique_ptr<tt_ClusterDescriptor> create_ethernet_map();

private:
    struct EthAddresses {
        uint32_t masked_version;

        uint64_t node_info;
        uint64_t eth_conn_info;
        uint64_t results_buf;
        uint64_t erisc_remote_board_type_offset;
        uint64_t erisc_local_board_type_offset;
        uint64_t erisc_local_board_id_lo_offset;
        uint64_t erisc_remote_board_id_lo_offset;
        uint64_t erisc_remote_eth_id_offset;
    };

    static EthAddresses get_eth_addresses(uint32_t eth_fw_version);

    void get_pcie_connected_chips();

    void discover_remote_chips();

    void fill_cluster_descriptor_info();

    bool is_pcie_chip_id_included(int pci_id) const;

    bool is_board_id_included(uint32_t board_id) const;

    // Returns mangled remote board id from local ETH core.
    // This information can still be used to unique identify a board.
    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // eth_core should be in physical (NOC0) coordinates.
    uint32_t get_remote_board_id(Chip* chip, tt_xy_pair eth_core);

    // Returns mangled local board id from local ETH core.
    // This information can still be used to unique identify a board.
    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // eth_core should be in physical (NOC0) coordinates.
    uint32_t get_local_board_id(Chip* chip, tt_xy_pair eth_core);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // eth_core should be in physical (NOC0) coordinates.
    uint64_t get_local_asic_id(Chip* chip, tt_xy_pair eth_core);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // eth_core should be in physical (NOC0) coordinates.
    uint64_t get_remote_asic_id(Chip* chip, tt_xy_pair eth_core);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    uint64_t get_asic_id(Chip* chip);

    // TODO: move this function to class specific for WH with old FW.
    eth_coord_t get_local_eth_coord(Chip* chip);

    // TODO: move this function to class specific for WH with old FW.
    // eth_core should be in physical (NOC0) coordinates.
    eth_coord_t get_remote_eth_coord(Chip* chip, tt_xy_pair eth_core);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // local_eth_core should be in physical (NOC0) coordinates.
    tt_xy_pair get_remote_eth_core(Chip* chip, tt_xy_pair local_eth_core);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // local_eth_core should be in physical (NOC0) coordinates.
    uint32_t get_remote_eth_id(Chip* chip, tt_xy_pair local_eth_core);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH..
    // eth_core should be in physical (NOC0) coordinates..
    uint32_t read_port_status(Chip* chip, tt_xy_pair eth_core, uint32_t channel);

    // TODO: override this logic for different configs. This is in group of functions
    // that we should override for T3K/6U/BH...
    // eth_core should be in physical (NOC0) coordinates.
    std::unique_ptr<RemoteChip> create_remote_chip(Chip* chip, tt_xy_pair eth_core, Chip* gateway_chip);

    Chip* get_chip(const chip_id_t chip_id);

    std::map<chip_id_t, std::unique_ptr<Chip>> chips_to_discover;
    std::map<chip_id_t, std::unique_ptr<Chip>> chips;

    std::unordered_map<uint64_t, chip_id_t> asic_id_to_chip_id;

    std::unordered_map<chip_id_t, eth_coord_t> eth_coords;

    std::vector<std::pair<std::pair<chip_id_t, uint32_t>, std::pair<chip_id_t, uint32_t>>> ethernet_connections;

    std::unique_ptr<tt_ClusterDescriptor> cluster_desc;

    chip_id_t chip_id = 0;

    EthAddresses eth_addresses;

    std::unordered_set<chip_id_t> pci_target_devices = {};

    // All board ids that should be included in the cluster descriptor.
    std::unordered_set<uint32_t> board_ids;

    std::unordered_map<chip_id_t, std::set<uint32_t>> active_eth_channels_per_chip;

    const std::string sdesc_path;

    bool is_running_on_6u = false;
};

}  // namespace tt::umd
