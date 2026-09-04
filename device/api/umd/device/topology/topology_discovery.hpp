// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/topology/discovery_protocol.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt::umd {

// TopologyDiscovery creates cluster descriptor after discovering all devices connected to the system.
//
// On silicon, everything architecture specific is delegated to a DiscoveryProtocol, and the discovery
// algorithm itself stays architecture agnostic. Backends that don't discover topology by reading
// ethernet state at all (e.g. simulation) can instead subclass this and override create_ethernet_map,
// get_connected_devices and discover_remote_devices, leaving the protocol null. Everything the base
// class shares with such a subclass - fill_cluster_descriptor_info, init_device, verify_fw_bundle_version,
// the chip id bookkeeping - works without a protocol; get_connected_devices and discover_remote_devices
// are the only two implementations here that require one.
class TopologyDiscovery {
public:
    static std::pair<std::unique_ptr<ClusterDescriptor>, std::map<ChipId, std::unique_ptr<TTDevice>>> discover(
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        const std::string& soc_descriptor_path = "");

    virtual ~TopologyDiscovery() = default;

protected:
    // discovery_protocol may be null, in which case get_connected_devices and discover_remote_devices
    // must both be overridden.
    TopologyDiscovery(
        std::shared_ptr<SocArchDescriptor> soc_arch_descriptor,
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        std::unique_ptr<DiscoveryProtocol> discovery_protocol = nullptr);

    static std::unique_ptr<TopologyDiscovery> create_topology_discovery(
        const TopologyDiscoveryOptions& options = {},
        IODeviceType io_device_type = IODeviceType::PCIe,
        const std::string& soc_descriptor_path = "");

    virtual std::unique_ptr<ClusterDescriptor> create_ethernet_map();

    // Requires a protocol.
    virtual void get_connected_devices();

    // Requires a protocol.
    virtual void discover_remote_devices();

    virtual std::unique_ptr<ClusterDescriptor> fill_cluster_descriptor_info();

    virtual bool is_board_id_included(uint64_t board_id) const;

    TTDevice* get_tt_device(const uint64_t asic_id);

    // No-op when there is no protocol to record the established version on.
    void verify_fw_bundle_version(TTDevice* tt_device, uint64_t asic_id);

    // Everything architecture specific about how ethernet state is discovered. Null for backends that
    // do not discover topology over ethernet.
    std::unique_ptr<DiscoveryProtocol> protocol;

    std::map<uint64_t, std::unique_ptr<TTDevice>> devices_to_discover;
    std::map<uint64_t, std::unique_ptr<TTDevice>> devices;
    std::unordered_map<uint64_t, ChipId> asic_id_to_chip_id;

    std::unordered_map<uint64_t, EthCoord> eth_coords;

    EthConnections ethernet_connections;

    EthConnections ethernet_connections_to_remote_devices;

    // All board ids that should be included in the cluster descriptor.
    std::unordered_set<uint64_t> board_ids;

    std::unordered_map<uint64_t, std::set<uint32_t>> active_eth_channels_per_device;

    // It's required to know which chip should be used for remote communication.
    std::map<uint64_t, uint64_t> remote_asic_id_to_mmio_device_id;

    std::map<uint64_t, std::vector<ClusterDescriptor::DeviceHealthError>> health_errors;

    const TopologyDiscoveryOptions options;
    const IODeviceType io_device_type = IODeviceType::PCIe;

    // A shared descriptor for all created SocDescriptors in TTDevices.
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor;

    // Initializes the device. On success returns true. On a recoverable initialization failure, logs
    // the error, records the structured error in health_errors keyed by the device's mocked (unhealthy)
    // ASIC ID, and returns false. Rethrows when device_init_failure_action is THROW.
    bool init_device(TTDevice* tt_device, ChipId chip_id, std::chrono::milliseconds timeout);

    ChipId get_next_chip_id() { return next_chip_id++; }

    // Mock ASIC ID assigned to unhealthy devices whose ASIC ID can't be determined.
    static constexpr uint64_t UNHEALTHY_ASIC_ID_PREFIX = 0xDEADDEAD;

    static uint64_t generate_unhealthy_asic_id(ChipId chip_id) { return chip_id | (UNHEALTHY_ASIC_ID_PREFIX << 32); }

    static bool is_marked_unhealthy(uint64_t asic_id) { return (asic_id >> 32) == (UNHEALTHY_ASIC_ID_PREFIX); }

private:
    // Next available ChipId.
    ChipId next_chip_id = 0;
};

}  // namespace tt::umd
