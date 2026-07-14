// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

// Bridges the simulator's metadata (SoC descriptor, cluster descriptor) and the GET_DEVICE_INFO /
// GET_CLUSTER_DESCRIPTOR wire messages, so a client can rebuild the host's device identity and
// topology without a local simulator build.

// Host side: package this device's SocDescriptor into a wire message, so a client can receive it
// and build its own matching SocDescriptor. backend_type records which simulator the host runs.
SimulationServerDeviceInfo describe_device(const SocDescriptor& soc_descriptor, SimulationBackendType backend_type);

// Client side: build a SocDescriptor from a wire message served by the host.
SocDescriptor build_soc_descriptor(const SimulationServerDeviceInfo& device_info);

// Client side: request the host's device identity over the socket.
SimulationServerDeviceInfo fetch_device_info_from_host(SimulationClient& client);

// Host side: read the simulator build's cluster-descriptor YAML as a wire message. Returns an empty
// `yaml` when the build ships no cluster_descriptor.yaml (the client then falls back to a mock).
SimulationServerClusterDescriptor describe_cluster(const std::filesystem::path& simulator_directory);

// Client side: request the host's cluster-descriptor YAML over an already-attached client. Attaches
// the client if needed; throws on a nonzero status. Empty string means the host has no descriptor.
std::string fetch_cluster_descriptor_yaml(SimulationClient& client);

}  // namespace tt::umd
