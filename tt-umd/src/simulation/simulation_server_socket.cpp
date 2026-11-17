// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_server_socket.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_server_socket_not_supported() {
    UMD_THROW(error::RuntimeError, "Serving simulation devices over sockets is not supported.");
}

}  // namespace

std::filesystem::path SimulationServerSocket::allocate_server_directory() { throw_server_socket_not_supported(); }

std::filesystem::path SimulationServerSocket::default_socket_path(const std::filesystem::path&, uint32_t) {
    throw_server_socket_not_supported();
}

std::unique_ptr<SimulationServerSocket> SimulationServerSocket::create(const std::filesystem::path&) {
    throw_server_socket_not_supported();
}

}  // namespace tt::umd
