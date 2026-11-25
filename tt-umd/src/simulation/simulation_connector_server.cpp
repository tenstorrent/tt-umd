// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_connector_server.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace tt::umd {

SimulationConnector::Server::Server(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SimulationConnector::Server::~Server() = default;

const std::filesystem::path& SimulationConnector::Server::server_directory() const { return impl_->server_directory; }

const std::map<ChipId, std::filesystem::path>& SimulationConnector::Server::sockets() const {
    return impl_->socket_paths;
}

}  // namespace tt::umd
