// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>

#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/blackhole_eth.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/types/core_coordinates.hpp"

namespace tt::umd {

/**
 * Topology discovery of Blackhole chips on the local PCIe bus.
 *
 * Every Blackhole chip is opened over PCIe; this build creates no remote (ETH-tunnelled) Blackhole devices. A link
 * to a chip that is not one of the local devices is recorded as a connection to a remote device, identified by that
 * chip's ASIC id. Blackhole has no ETH coordinates and no routing firmware.
 *
 * The ETH cores describe both ends of their link in L1: the local chip and, once the link trained, the chip at the
 * other end.
 */
class TopologyDiscoveryBlackhole : public TopologyDiscovery {
public:
    TopologyDiscoveryBlackhole(
        std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
        const TopologyDiscoveryOptions& options,
        IODeviceType io_device_type);

protected:
    tt::ARCH get_topology_arch() const override;

    uint64_t get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint64_t get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) override;

    uint32_t get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) override;

    uint32_t get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) override;

    // The ASIC id the firmware publishes in telemetry, which needs no trained ETH link.
    uint64_t get_unconnected_device_id(TTDevice* tt_device) override;

    std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) override;

    std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) override;

    // The physical channel of the remote core: its index among all ETH cores, harvested ones included.
    uint32_t get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) override;

    uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) override;

    bool is_using_eth_coords() override;

    std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord,
        TTDevice* gateway_device,
        std::set<uint32_t> gateway_eth_channels,
        const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor = nullptr) override;

    void init_first_device(TTDevice* tt_device) override;

    void verify_routing_firmware_state(TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) override;

    // Converts the remote end of every link between two local devices from the physical channel the ETH core
    // reports to the logical channel the remote device's SocDescriptor uses.
    void patch_eth_connections() override;

    void retrain_eth_cores() override;

private:
    blackhole::chip_info_t read_local_chip_info(TTDevice* tt_device, CoreCoord eth_core);

    blackhole::chip_info_t read_remote_chip_info(TTDevice* tt_device, CoreCoord eth_core);
};

}  // namespace tt::umd
