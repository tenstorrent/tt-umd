// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_server_socket.hpp"

#include <filesystem>
#include <memory>

#include "common/unsupported.hpp"

namespace tt::umd {

std::filesystem::path SimulationServerSocket::allocate_server_directory() { UMD_NOT_SUPPORTED("Simulation"); }

std::filesystem::path SimulationServerSocket::default_socket_path(
    const std::filesystem::path& server_directory, ChipId chip_id) {
    UMD_NOT_SUPPORTED("Simulation");
}

std::unique_ptr<SimulationServerSocket> SimulationServerSocket::create(const std::filesystem::path& socket_path) {
    UMD_NOT_SUPPORTED("Simulation");
}

SimulationServerSocket::~SimulationServerSocket() = default;

}  // namespace tt::umd
