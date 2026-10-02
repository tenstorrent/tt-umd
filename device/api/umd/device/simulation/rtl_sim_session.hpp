// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

/**
 * One run of an RTL simulator build: launches <simulator_directory>/run.sh once and serves one
 * socket per name, each the host side of one access point. See SimulationHost::init() for the env
 * variables each name uses; an empty name is the single unnamed socket. Names must be distinct. With
 * launch_simulator false, it only serves its sockets and another process launches run.sh (the run
 * then spans several host processes). RtlSimCommunicator speaks the device protocol over one of
 * these sockets; a device is handed its socket as an RtlSimSocket.
 */
class RtlSimSession {
public:
    RtlSimSession(
        const std::filesystem::path& simulator_directory,
        const std::vector<std::string>& sockets,
        bool launch_simulator = true);

    /**
     * Create every listener, launch run.sh if launching, then wait for the ack on each socket in
     * order. Once only.
     * Throws if a socket does not ack within @p ack_timeout.
     */
    void start(std::chrono::milliseconds ack_timeout = timeout::RTL_SIM_ACK_TIMEOUT);

    size_t get_num_sockets() const;

    /** Socket @p socket's host; the session must have been started. */
    SimulationHost& get_host(size_t socket);

private:
    std::filesystem::path simulator_directory_;
    std::vector<std::string> sockets_;
    std::vector<std::unique_ptr<SimulationHost>> hosts_;
    bool launch_simulator_;
    bool started_ = false;
};

/**
 * One socket of a started RtlSimSession: the access point a device talks over. It keeps the session
 * alive, so the run outlives every device using one of its sockets.
 */
class RtlSimSocket {
public:
    RtlSimSocket(std::shared_ptr<RtlSimSession> session, size_t socket);

    SimulationHost& get_host() const;

private:
    std::shared_ptr<RtlSimSession> session_;
    size_t socket_;
};

}  // namespace tt::umd
