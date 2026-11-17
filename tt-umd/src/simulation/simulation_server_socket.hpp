// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>

#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

// Listening UNIX socket a simulation host serves one chip on. Clients connect with SimulationClient.
class SimulationServerSocket {
public:
    // Creates a fresh, private directory to serve sockets in, so hosts on one machine never collide.
    static std::filesystem::path allocate_server_directory();

    // The socket path a host serves chip_id on inside server_directory, and that clients look for.
    static std::filesystem::path default_socket_path(const std::filesystem::path& server_directory, ChipId chip_id);

    // Binds and listens on socket_path.
    static std::unique_ptr<SimulationServerSocket> create(const std::filesystem::path& socket_path);

    ~SimulationServerSocket();

    SimulationServerSocket(const SimulationServerSocket&) = delete;
    SimulationServerSocket& operator=(const SimulationServerSocket&) = delete;

private:
    SimulationServerSocket() = default;
};

}  // namespace tt::umd
