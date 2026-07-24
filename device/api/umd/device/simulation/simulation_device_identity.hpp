// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/soc_descriptor.hpp"

namespace tt::umd {

// Bridges a SocDescriptor and the GET_DEVICE_INFO wire message, so a client can rebuild the host's
// device identity without a local simulator build.

// Host side: package this device's SocDescriptor into a wire message, so a client can receive it
// and build its own matching SocDescriptor. backend_type records which simulator the host runs.
SimulationServerDeviceInfo describe_device(const SocDescriptor& soc_descriptor, SimulationBackendType backend_type);

// Client side: build a SocDescriptor from a wire message served by the host.
SocDescriptor build_soc_descriptor(const SimulationServerDeviceInfo& device_info);

// Client side: request the host's device identity over the socket.
SimulationServerDeviceInfo fetch_device_info_from_host(SimulationClient& client);

}  // namespace tt::umd
