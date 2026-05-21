// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class TopologyDiscovery {
public:
    /**
     * @brief Discovers all Tenstorrent devices in the system and builds the cluster topology.
     *
     * Enumerates local devices (PCIe or JTAG), then walks ethernet links to find remote devices.
     * Returns a ClusterDescriptor with the full topology and a map of all discovered TTDevice
     * instances keyed by ChipId. Returns empty results if no devices are found.
     *
     * @param options Error handling policy and feature toggles for the discovery process.
     * @param io_device_type Transport used to reach local devices (PCIe or JTAG).
     * @param soc_descriptor_path Optional SoC descriptor file override.
     * @return ClusterDescriptor and a map of ChipId to TTDevice for every discovered device.
     */
    static std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> discover(
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        const std::string& soc_descriptor_path = "");

    /** @brief Virtual destructor for safe polymorphic cleanup. */
    virtual ~TopologyDiscovery() = default;

protected:
    TopologyDiscovery(
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        const std::string& soc_descriptor_path = "");

    static std::unique_ptr<TopologyDiscovery> create_topology_discovery(
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        const std::string& soc_descriptor_path = "");

    virtual tt::ARCH get_topology_arch() const = 0;

    std::unique_ptr<ClusterDescriptor> create_ethernet_map();

    void get_connected_devices();

    void discover_remote_devices();

    std::unique_ptr<ClusterDescriptor> fill_cluster_descriptor_info();

    virtual void wait_eth_cores_training(
        TTDevice* tt_device, std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);

    virtual bool is_board_id_included(uint64_t board_id) const;

    virtual uint64_t get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint64_t get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint64_t get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint64_t get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual bool is_eth_port_disabled(TTDevice* tt_device, CoreCoord eth_core) { return false; }

    virtual bool eth_heartbeat_running(TTDevice* tt_device, CoreCoord eth_core);

    virtual uint32_t get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint32_t get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) = 0;

    uint64_t get_asic_id(TTDevice* tt_device);

    virtual uint64_t get_unconnected_device_id(TTDevice* tt_device) = 0;

    virtual std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint32_t get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) = 0;

    virtual uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) = 0;

    virtual bool is_using_eth_coords() = 0;

    virtual std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord, TTDevice* gateway_device, std::set<uint32_t> gateway_eth_channels) = 0;

    TTDevice* get_tt_device(const uint64_t asic_id);

    virtual void init_first_device(TTDevice* tt_device) = 0;

    bool is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core);

    virtual void verify_routing_firmware_state(TTDevice* tt_device, const CoreCoord eth_core) = 0;

    virtual void patch_eth_connections();

    virtual void retrain_eth_cores() = 0;

    std::map<uint64_t, std::unique_ptr<TTDevice>> devices_to_discover;
    std::map<uint64_t, std::unique_ptr<TTDevice>> devices;
    std::unordered_map<uint64_t, ChipId> asic_id_to_chip_id;

    std::unordered_map<uint64_t, EthCoord> eth_coords;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>>
        ethernet_connections_to_remote_devices;

    std::unordered_set<uint64_t> board_ids;

    std::unordered_map<uint64_t, std::set<uint32_t>> active_eth_channels_per_device;

    std::map<uint64_t, uint64_t> remote_asic_id_to_mmio_device_id;

    bool is_running_on_6u = false;

    const TopologyDiscoveryOptions options;
    const IODeviceType io_device_type = IODeviceType::PCIe;
    const std::string& soc_descriptor_path = "";

    virtual bool verify_eth_core_fw_version(TTDevice* tt_device, CoreCoord eth_core) = 0;

    void verify_fw_bundle_version(TTDevice* tt_device);

    std::optional<SemVer> expected_eth_fw_version;

    std::optional<FirmwareBundleVersion> first_fw_bundle_version;

private:
    ChipId next_chip_id = 0;

    ChipId get_next_chip_id() { return next_chip_id++; }

    static constexpr uint64_t UNHEALTHY_ASIC_ID_PREFIX = 0xDEADDEAD;

    static uint64_t generate_unhealthy_asic_id(ChipId chip_id) { return chip_id | (UNHEALTHY_ASIC_ID_PREFIX << 32); }

    static bool is_marked_unhealthy(uint64_t asic_id) { return (asic_id >> 32) == (UNHEALTHY_ASIC_ID_PREFIX); }
};

}  // namespace tt::umd
