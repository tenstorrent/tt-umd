// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/simulation/simulation_client.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

SimulationClient::SimulationClient(std::filesystem::path socket_path, std::chrono::milliseconds timeout) {
    UMD_NOT_SUPPORTED("Simulation");
}

// The asio transport, which this build never creates.
struct SimulationClient::Impl {};

SimulationClient::~SimulationClient() = default;

void SimulationClient::attach() { UMD_NOT_SUPPORTED("Simulation"); }

void SimulationClient::detach() { UMD_NOT_SUPPORTED("Simulation"); }

std::vector<uint8_t> SimulationClient::transact(const std::vector<uint8_t>& request) {
    UMD_NOT_SUPPORTED("Simulation");
}

}  // namespace tt::umd
