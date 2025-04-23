/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/tt_device/tt_device.h"

class tt_ClusterDescriptor;

namespace tt::umd {

// TopologyDiscovery class creates cluster descriptor only for Wormhole configurations with old routing fw.
// TODO: Move Blackhole and 6U topology discovery to this class.
class TopologyDiscovery {
public:
    std::unique_ptr<tt_ClusterDescriptor> create_ethernet_map();

private:
    struct EthAddresses {
        uint32_t masked_version;

        uint64_t version;
        uint64_t boot_params;
        uint64_t node_info;
        uint64_t eth_conn_info;
        uint64_t debug_buf;
        uint64_t results_buf;
        bool shelf_rack_routing;
        uint64_t heartbeat;
        uint64_t erisc_app;
        uint64_t erisc_app_config;
        uint64_t erisc_remote_board_type_offset;
        uint64_t erisc_local_board_type_offset;
    };

    static EthAddresses get_eth_addresses(uint32_t eth_fw_version);

    void get_pcie_connected_chips();

    void discover_remote_chips();

    void fill_cluster_descriptor_info();

    // TODO: this should be moved to class similar to TTDevice for MMIO devices.
    // Covered by the UMD issue https://github.com/tenstorrent/tt-umd/issues/730.
    uint32_t remote_arc_msg(
        eth_coord_t eth_coord,
        uint32_t msg_code,
        uint32_t arg0,
        uint32_t arg1,
        uint32_t* ret0,
        uint32_t* ret1,
        Chip* mmio_chip,
        uint32_t timeout_ms = 5000);

    // TODO: this should be moved to class similar to TTDevice for MMIO devices.
    // Covered by the UMD issue https://github.com/tenstorrent/tt-umd/issues/730.
    ChipInfo read_non_mmio_chip_info(eth_coord_t eth_coord, Chip* mmio_chip);

    // TODO: this should be moved to class similar to TTDevice for MMIO devices.
    // Covered by the UMD issue https://github.com/tenstorrent/tt-umd/issues/730.
    BoardType get_board_type(eth_coord_t eth_coord, Chip* mmio_chip);

    std::unordered_map<chip_id_t, std::unique_ptr<Chip>> chips;

    std::unordered_map<eth_coord_t, chip_id_t> eth_coord_to_chip_id;

    std::unordered_map<chip_id_t, eth_coord_t> eth_coords;

    // Remote transfer eth cores for each TTDevice, key of the map is pcie device that we
    // create tt device for.
    std::unordered_map<uint32_t, std::vector<tt_xy_pair>> remote_transfer_ethernet_cores;

    std::vector<std::pair<std::pair<chip_id_t, uint32_t>, std::pair<chip_id_t, uint32_t>>> ethernet_connections;

    std::unique_ptr<tt_ClusterDescriptor> cluster_desc;

    chip_id_t chip_id = 0;

    EthAddresses eth_addresses;
};

}  // namespace tt::umd
