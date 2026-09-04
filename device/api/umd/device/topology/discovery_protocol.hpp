// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/semver.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

// {{local asic id, local eth channel}, {remote asic id, remote eth channel}}
using EthConnection = std::pair<std::pair<uint64_t, uint32_t>, std::pair<uint64_t, uint32_t>>;
using EthConnections = std::vector<EthConnection>;

// DiscoveryProtocol encapsulates everything TopologyDiscovery needs to know about how a given
// architecture exposes its ethernet state. TopologyDiscovery drives the discovery algorithm and
// delegates every ethernet query to the protocol, so the algorithm itself stays architecture
// agnostic.
class DiscoveryProtocol {
public:
    virtual ~DiscoveryProtocol() = default;

    // Creates the protocol implementation matching the given architecture.
    static std::unique_ptr<DiscoveryProtocol> create(tt::ARCH arch, const TopologyDiscoveryOptions& options);

    // Looks up a TTDevice by its ASIC ID. Provided by TopologyDiscovery, since the protocol does
    // not own the discovered devices.
    using DeviceLookup = std::function<TTDevice*(uint64_t asic_id)>;

    virtual tt::ARCH get_topology_arch() const = 0;

    // Configure some protocol parameters from the first discovered device.
    virtual void init_first_device(TTDevice* tt_device) = 0;

    // Returns mangled remote board id from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_remote_board_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    // Returns mangled local board id from local ETH core.
    // This information can still be used to unique identify a board.
    // eth_core should be in physical (NOC0) coordinates.
    virtual uint64_t get_local_board_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual uint64_t get_local_asic_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual uint64_t get_remote_asic_id(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint64_t get_unconnected_device_id(TTDevice* tt_device) = 0;

    // Returns a unique ID for the device, derived from an active ETH core when there is one.
    uint64_t get_asic_id(TTDevice* tt_device);

    virtual std::optional<EthCoord> get_local_eth_coord(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual std::optional<EthCoord> get_remote_eth_coord(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint32_t get_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) = 0;

    // API exposed as a temporary workaround for issue: https://tenstorrent.atlassian.net/browse/SYS-2064.
    // This is used for querying the logical remote eth channel on Multi-Host Blackhole P150 systems, where
    // we don't have access to the ethernet harvesting mask for the remote device.
    // Logic in this API can be placed in get_remote_eth_channel, and patch_eth_connections can be removed,
    // once the issue outlined in the ticket is resolved (at which point, UMD can directly query the logical
    // ethernet channel for the remote device on all board types).
    virtual uint32_t get_logical_remote_eth_channel(TTDevice* tt_device, CoreCoord local_eth_core) = 0;

    virtual bool is_using_eth_coords() = 0;

    virtual bool is_eth_port_disabled(TTDevice* tt_device, CoreCoord eth_core) { return false; }

    bool is_eth_trained(TTDevice* tt_device, const CoreCoord eth_core);

    virtual uint32_t get_eth_heartbeat(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual uint32_t get_eth_postcode(TTDevice* tt_device, CoreCoord eth_core) = 0;

    virtual bool eth_heartbeat_running(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core);

    virtual void wait_eth_cores_training(
        TTDevice* tt_device, std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);

    virtual void verify_routing_firmware_state(TTDevice* tt_device, uint64_t asic_id, const CoreCoord eth_core) = 0;

    virtual bool verify_eth_core_fw_version(TTDevice* tt_device, uint64_t asic_id, CoreCoord eth_core);

    virtual void retrain_eth_cores(const std::map<uint64_t, std::unique_ptr<TTDevice>>& devices) = 0;

    // eth_core should be in NoC 0 coordinates.
    virtual std::unique_ptr<TTDevice> create_remote_device(
        std::optional<EthCoord> eth_coord,
        TTDevice* gateway_device,
        std::set<uint32_t> gateway_eth_channels,
        const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor = nullptr) = 0;

    // This is hack to report proper logical ETH IDs, since eth id on ETH core on Blackhole
    // does not take harvesting into consideration. This function will be overridden just for Blackhole.
    virtual void patch_eth_connections(EthConnections& ethernet_connections, const DeviceLookup& device_lookup);

    // TopologyDiscovery owns the health error map, the protocol only appends to it.
    void set_health_error_sink(std::map<uint64_t, std::vector<ClusterDescriptor::DeviceHealthError>>* sink) {
        health_errors = sink;
    }

    // The expected ETH FW version, matching the version shipped in the firmware bundle.
    // If there is no available expected version, we use the version from the first discovered local device.
    const std::optional<SemVer>& get_expected_eth_fw_version() const { return expected_eth_fw_version; }

    // The FW bundle version found on the first discovered local device, that needs
    // to match with all of the other discovered FW bundle versions on all devices.
    const std::optional<FirmwareBundleVersion>& get_fw_bundle_version() const { return first_fw_bundle_version; }

    void set_fw_bundle_version(FirmwareBundleVersion fw_bundle_version) { first_fw_bundle_version = fw_bundle_version; }

protected:
    explicit DiscoveryProtocol(const TopologyDiscoveryOptions& options) : options(options) {}

    void add_health_error(uint64_t asic_id, ClusterDescriptor::DeviceHealthError error);

    const TopologyDiscoveryOptions options;

    bool is_running_on_6u = false;

    std::optional<SemVer> expected_eth_fw_version;

    std::optional<FirmwareBundleVersion> first_fw_bundle_version;

private:
    std::map<uint64_t, std::vector<ClusterDescriptor::DeviceHealthError>>* health_errors = nullptr;
};

}  // namespace tt::umd
