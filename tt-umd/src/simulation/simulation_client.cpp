// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Connecting to a simulation host is not supported by this build: attaching always fails.

#include "tt-umd/simulation/simulation_client.hpp"

#include <utility>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

// The connection never opens, so there is no transport state to hold.
struct SimulationClient::Impl {};

namespace {

[[noreturn]] void throw_simulation_client_not_supported() {
    UMD_THROW(error::RuntimeError, "Connecting to a simulation host is not supported.");
}

}  // namespace

SimulationClient::SimulationClient(std::filesystem::path socket_path, std::chrono::milliseconds timeout) :
    socket_path_(std::move(socket_path)), timeout_(timeout) {}

SimulationClient::~SimulationClient() = default;

void SimulationClient::attach() { throw_simulation_client_not_supported(); }

void SimulationClient::detach() {}

std::vector<uint8_t> SimulationClient::transact(const std::vector<uint8_t>&) {
    throw_simulation_client_not_supported();
}

}  // namespace tt::umd
