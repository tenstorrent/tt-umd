// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <optional>
#include <unordered_set>

#include "umd/device/chip/chip.hpp"
#include "umd/device/chip/remote_chip.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class ClusterDescriptor;

struct TopologyDiscoveryOptions {
    // Path to custom SoC descriptor when creating devices. See ClusterOptions.
    std::string soc_descriptor_path;

    // I/O device type to use when discovering. See ClusterOptions.
    IODeviceType io_device_type = IODeviceType::PCIe;

    // Skip discovery of devices connected via Ethernet.
    bool no_remote_discovery = false;

    // Skip waiting for ETH core training.
    bool no_wait_for_eth_training = false;

    // Allow unsupported ETH firmware versions and do not fail when
    // cores have different ETH firmware versions.
    bool no_eth_firmware_strictness = false;

    // Predict ETH firmware version for entire cluster from the known
    // ETH firmware version bundled with the firmware bundle.
    bool predict_eth_fw_version = false;

    // Enables verifying ERISC FW on cores to ensure reliability of discovery.
    bool verify_eth_fw_hash = false;

    // Preferred architecture.
    std::optional<tt::ARCH> architecture = std::nullopt;
};

// TopologyDiscovery creates cluster descriptor after discovering all devices connected to the system.
class TopologyDiscovery {
public:
    static std::pair<std::unique_ptr<ClusterDescriptor>, std::map<uint64_t, std::unique_ptr<TTDevice>>> discover(
        const TopologyDiscoveryOptions& options);

    virtual ~TopologyDiscovery() = default;

protected:
    TopologyDiscovery(const TopologyDiscoveryOptions& options);

    static std::unique_ptr<TopologyDiscovery> create_topology_discovery(const TopologyDiscoveryOptions& options);

    std::unique_ptr<ClusterDescriptor> create_ethernet_map();

    void get_connected_devices();

    void discover_remote_devices();

    std::unique_ptr<ClusterDescriptor> fill_cluster_descriptor_info();

    virtual void wait_eth_cores_training(
        TTDevice* tt_device, std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);

    // board_type is not used for all configs.
    // We need to know that we are seeing TG board and that we should include it in the topology.
    virtual bool is_board_id_included(uint64_t board_id, uint64_t board_type) const = 0;

    // Returns mangled remote board id from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_remote_board_id(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    // Returns mangled remote board type from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_remote_board_type(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    // Returns mangled local board id from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_local_board_id(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual uint64_t get_local_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual uint64_t get_remote_asic_id(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    uint64_t get_asic_id(TTDevice* tt_device);

    virtual uint64_t get_unconnected_device_id(TTDevice* tt_device) = 0;

    virtual std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    virtual std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    // local_eth_core should be in NoC 0 coordinates.
    virtual tt_xy_pair get_remote_eth_core(TTDevice* tt_device, tt_xy_pair local_eth_core) = 0;

    // local_eth_core should be in NoC 0 coordinates.
    virtual uint32_t get_remote_eth_id(TTDevice* tt_device, tt_xy_pair local_eth_core) = 0;

    virtual uint32_t get_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) = 0;

    // API exposed as a temporary workaround for issue: https://tenstorrent.atlassian.net/browse/SYS-2064.
    // This is used for querying the logical remote eth channel on Multi-Host Blackhole P150 systems, where
    // we don't have access to the ethernet harvesting mask for the remote device.
    // Logic in this API can be placed in get_remote_eth_channel, and patch_eth_connections can be removed,
    // once the issue outlined in the ticket is resolved (at which point, UMD can directly query the logical
    // ethernet channel for the remote device on all board types).
    virtual uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, tt_xy_pair local_eth_core) = 0;

    virtual bool is_using_eth_coords() = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord, TTDevice* gateway_device, std::set<uint32_t> gateway_eth_channels) = 0;

    TTDevice* get_tt_device(const uint64_t asic_id);

    // Configure some TopologyDiscovery paramaters from first discovered device.
    virtual void init_first_device(TTDevice* tt_device) = 0;

    virtual bool is_eth_trained(TTDevice* tt_device, const tt_xy_pair eth_core) = 0;

    virtual bool verify_routing_firmware_state(TTDevice* tt_device, const tt_xy_pair eth_core) = 0;

    // This is hack to report proper logical ETH IDs, since eth id on ETH core on Blackhole
    // does not take harvesting into consideration. This function will be overridden just for Blackhole.
    virtual void patch_eth_connections();

    std::map<uint64_t, std::unique_ptr<TTDevice>> devices_to_discover;
    std::map<uint64_t, std::unique_ptr<TTDevice>> devices;
    SocDescriptor get_soc_descriptor(TTDevice* tt_device);

    std::unordered_map<uint64_t, EthCoord> eth_coords;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>> ethernet_connections;

    std::vector<std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>>
        ethernet_connections_to_remote_devices;

    // All board ids that should be included in the cluster descriptor.
    std::unordered_set<uint64_t> board_ids;

    std::unordered_map<uint64_t, std::set<uint32_t>> active_eth_channels_per_device;

    // It's required to know which chip should be used for remote communication.
    std::map<uint64_t, uint64_t> remote_asic_id_to_mmio_device_id;

    TopologyDiscoveryOptions options;

    bool is_running_on_6u = false;

    virtual bool verify_eth_core_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) = 0;

    virtual bool verify_fw_bundle_version(TTDevice* tt_device);

    // The expected ETH FW version, matching the version shipped in the firmware bundle.
    // If there is no available expected version, we use the version from the first discovered local device.
    std::optional<semver_t> expected_eth_fw_version;

    // The FW bundle version found on the first discovered local device, that needs
    // to match with all of the other discovered FW bundle versions on all devices.
    std::optional<semver_t> first_fw_bundle_version;

private:
    // Hack used to cache SocDescriptors.
    std::unordered_map<TTDevice*, SocDescriptor> soc_descriptor_cache;

    static tt::ARCH determine_architecture(IODeviceType io_device_type);
};

}  // namespace tt::umd
