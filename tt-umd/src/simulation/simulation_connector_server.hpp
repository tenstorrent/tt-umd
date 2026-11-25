// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Defined apart from SimulationConnector::serve, which only a build with simulation has, because a Server is
// destroyed in every build: Cluster holds one.
struct SimulationConnector::Server::Impl {
    std::filesystem::path server_directory;
    std::map<ChipId, std::filesystem::path> socket_paths;
    // One per served device. Each owns its request handler, so destroying it is all it takes to stop serving.
    std::vector<std::unique_ptr<SimulationServerSocket>> sockets;
};

}  // namespace tt::umd
