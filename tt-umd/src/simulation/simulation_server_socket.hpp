// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace tt::umd {

/**
 * @brief Listening UNIX socket a simulation host serves one chip on.
 *
 * Serving simulation devices over sockets is not supported by this build: every member throws.
 */
class SimulationServerSocket {
public:
    // Creates a fresh directory to hold the server sockets of one simulation host.
    static std::filesystem::path allocate_server_directory();

    // The socket path a host serves chip_id on, inside server_directory.
    static std::filesystem::path default_socket_path(const std::filesystem::path& server_directory, uint32_t chip_id);

    // Binds and listens on socket_path.
    static std::unique_ptr<SimulationServerSocket> create(const std::filesystem::path& socket_path);
};

}  // namespace tt::umd
