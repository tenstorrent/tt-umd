// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_sim_session.hpp"

#include <fmt/format.h>
#include <nng/nng.h>
#include <uv.h>

#include <algorithm>
#include <limits>
#include <set>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "simulation_device_generated.h"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

void spawn_simulator(const std::filesystem::path &simulator_directory) {
    const std::string run_script = simulator_directory / "run.sh";
    UMD_ASSERT(
        std::filesystem::exists(run_script),
        error::RuntimeError,
        fmt::format("Simulator binary not found at: {}", run_script));

    uv_loop_t *loop = uv_default_loop();

    uv_stdio_container_t child_stdio[3];
    child_stdio[0].flags = UV_IGNORE;
    child_stdio[1].flags = UV_INHERIT_FD;
    child_stdio[1].data.fd = 1;
    child_stdio[2].flags = UV_INHERIT_FD;
    child_stdio[2].data.fd = 2;

    uv_process_options_t child_options = {nullptr};
    child_options.file = run_script.c_str();
    child_options.flags = UV_PROCESS_DETACHED;
    child_options.stdio_count = 3;
    child_options.stdio = child_stdio;

    uv_process_t child_p;
    int rv = uv_spawn(loop, &child_p, &child_options);
    UMD_ASSERT(rv == 0, error::RuntimeError, fmt::format("Failed to spawn simulator process: {}", uv_strerror(rv)));
    log_info(tt::LogEmulationDriver, "Simulator process spawned with PID: {}", child_p.pid);

    // Stop tracking the detached child before child_p goes out of scope; otherwise libuv's SIGCHLD
    // handling writes into this stack frame when run.sh exits.
    uv_close(reinterpret_cast<uv_handle_t *>(&child_p), nullptr);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
}

}  // namespace

RtlSimSession::RtlSimSession(
    const std::filesystem::path &simulator_directory, const std::vector<std::string> &sockets, bool launch_simulator) :
    simulator_directory_(simulator_directory), sockets_(sockets), launch_simulator_(launch_simulator) {
    UMD_ASSERT(!sockets_.empty(), error::RuntimeError, "RtlSimSession needs at least one socket.");
    // Each name is a process-wide NNG_SOCKET_ADDR_<name> variable, so a repeated one would leave a
    // listener nobody can reach.
    UMD_ASSERT(
        std::set<std::string>(sockets_.begin(), sockets_.end()).size() == sockets_.size(),
        error::RuntimeError,
        "RtlSimSession socket names must be distinct.");
    for (size_t i = 0; i < sockets_.size(); i++) {
        hosts_.push_back(std::make_unique<SimulationHost>());
    }
}

void RtlSimSession::start(std::chrono::milliseconds ack_timeout) {
    UMD_ASSERT(!started_, error::RuntimeError, "RtlSimSession::start() called twice.");
    started_ = true;
    for (size_t i = 0; i < sockets_.size(); i++) {
        hosts_[i]->init(sockets_[i]);
    }

    if (launch_simulator_) {
        spawn_simulator(simulator_directory_);
    }

    for (auto &host : hosts_) {
        host->start_host();
    }

    for (size_t i = 0; i < sockets_.size(); i++) {
        log_info(tt::LogEmulationDriver, "Waiting for ack msg from remote on socket '{}'...", sockets_[i]);
        void *buf_ptr = nullptr;
        // NNG takes the timeout as an int of milliseconds.
        const int timeout_ms = static_cast<int>(
            std::clamp<std::chrono::milliseconds::rep>(ack_timeout.count(), 0, std::numeric_limits<int>::max()));
        size_t buf_size = hosts_[i]->recv_from_device(&buf_ptr, timeout_ms);
        UMD_ASSERT(
            buf_size != 0,
            error::RuntimeError,
            fmt::format(
                "No ack from remote on socket '{}' (timed out after {} ms, or the receive failed); check the "
                "simulator log.",
                sockets_[i],
                timeout_ms));
        const bool is_ack = GetDeviceRequestResponse(buf_ptr)->command() == DEVICE_COMMAND_EXIT;
        nng_free(buf_ptr, buf_size);
        UMD_ASSERT(
            is_ack,
            error::RuntimeError,
            fmt::format("Did not receive expected ack from remote on socket '{}'.", sockets_[i]));
    }
}

size_t RtlSimSession::get_num_sockets() const { return sockets_.size(); }

SimulationHost &RtlSimSession::get_host(size_t socket) {
    UMD_ASSERT(started_, error::RuntimeError, "RtlSimSession::get_host() before start().");
    return *hosts_.at(socket);
}

RtlSimSocket::RtlSimSocket(std::shared_ptr<RtlSimSession> session, size_t socket) :
    session_(std::move(session)), socket_(socket) {
    UMD_ASSERT(session_ != nullptr, error::RuntimeError, "RtlSimSocket needs a session.");
    UMD_ASSERT(
        socket_ < session_->get_num_sockets(),
        error::RuntimeError,
        fmt::format("RtlSimSocket {} is out of range; the session has {}.", socket_, session_->get_num_sockets()));
}

SimulationHost &RtlSimSocket::get_host() const { return session_->get_host(socket_); }

}  // namespace tt::umd
