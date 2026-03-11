// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TopologyDiscoveryWormhole : public TopologyDiscovery {
public:
    TopologyDiscoveryWormhole(
        const TopologyDiscoveryOptions& options, IODeviceType io_device_type, const std::string& soc_descriptor_path);

protected:
    tt::ARCH get_topology_arch() const override { return tt::ARCH::WORMHOLE_B0; }

    struct EthAddresses {
        static constexpr uint64_t ETH_PARAM_TABLE = 0x1000;
        static constexpr uint64_t ROUTING_FIRMWARE_STATE = 0x104c;
        static constexpr uint64_t NODE_INFO = 0x1100;
        static constexpr uint64_t ETH_CONN_INFO = 0x1200;
        static constexpr uint64_t RESULTS_BUF = 0x1ec0;
        static constexpr uint64_t ERISC_REMOTE_BOARD_TYPE_OFFSET = 77;
        static constexpr uint64_t ERISC_LOCAL_BOARD_TYPE_OFFSET = 69;
        static constexpr uint64_t ERISC_LOCAL_BOARD_ID_LO_OFFSET = 64;
        static constexpr uint64_t ERISC_REMOTE_BOARD_ID_LO_OFFSET = 72;
        static constexpr uint64_t ERISC_REMOTE_ETH_ID_OFFSET = 76;
    };

    uint64_t get_remote_board_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_local_board_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_local_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_remote_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint64_t get_unconnected_device_id(TTDevice* tt_device) override;

    std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) override;

    std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) override;

    tt_xy_pair get_remote_eth_core(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint32_t get_remote_eth_id(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint32_t get_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) override;

    std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord, TTDevice* gateway_device, std::set<uint32_t> gateway_eth_channels) override;

    bool is_using_eth_coords() override;

    void init_first_device(TTDevice* tt_device) override;

    bool is_eth_trained(TTDevice* tt_device, const tt_xy_pair eth_core) override;

    void verify_routing_firmware_state(TTDevice* tt_device, const tt_xy_pair eth_core) override;

    bool verify_eth_core_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint32_t get_eth_heartbeat(TTDevice* tt_device, tt_xy_pair eth_core) override;

    uint32_t get_eth_postcode(TTDevice* tt_device, tt_xy_pair eth_core) override;

    static constexpr uint32_t LINK_TRAIN_SUCCESS = 1;
    static constexpr uint32_t LINK_TRAIN_TRAINING = 0;
};
}  // namespace tt::umd
