// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// sim_server: manage long-running simulation host processes.
//
//   sim_server start <simulator.so | rtl-dir>   -- daemonize a host that serves the simulation in a
//                                                  fresh server directory; other UMD processes attach
//                                                  as clients (a Cluster pointed at that directory).
//   sim_server list                              -- list the currently-open servers and their chips.
//   sim_server kill <server>                     -- ask a server to shut down, over its socket.
//
// Each host gets its own server directory, so two hosts (even serving the same chip id) never
// collide; list/kill address a server by its index. Management is in-band over the socket (a
// SHUTDOWN request), not by PID/signal: the socket is world-writable and cross-user, so a socket
// message reaches exactly the processes that can reach the server, whereas a signal would be
// same-uid only.

#include <fcntl.h>
#include <fmt/format.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cxxopts.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/cluster.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/simulation/simulation_device_identity.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/tt_device/simulation_tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

namespace {

// Self-pipe used to unblock the daemon's main thread from either stop source: a SHUTDOWN request
// (handled on a serving thread) or a SIGTERM/SIGINT. Both just write one byte; the main thread reads.
// The write end is non-blocking (set in cmd_start) so writing from a signal handler cannot block.
int g_stop_pipe[2] = {-1, -1};

void request_stop() {
    if (g_stop_pipe[1] >= 0) {
        const char byte = 1;
        // async-signal-safe; the write end is non-blocking, so a full pipe fails with EAGAIN rather
        // than blocking -- fine, since a full pipe already holds a pending wakeup byte.
        [[maybe_unused]] const ssize_t written = ::write(g_stop_pipe[1], &byte, 1);
    }
}

void on_signal(int /*signum*/) { request_stop(); }

// Detaches stdio to a per-process log so the daemon isn't tied to the launching terminal.
void redirect_stdio_to_log() {
    const std::filesystem::path log_path =
        std::filesystem::temp_directory_path() / fmt::format("tt-umd-sim-server-{}.log", ::getpid());
    const int log_fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    const int null_fd = ::open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
        ::dup2(null_fd, STDIN_FILENO);
        ::close(null_fd);
    }
    if (log_fd >= 0) {
        ::dup2(log_fd, STDOUT_FILENO);
        ::dup2(log_fd, STDERR_FILENO);
        ::close(log_fd);
    }
}

int cmd_start(const std::string& simulator_path) {
    // Claim the server directory before forking so the parent can report where the host will serve
    // (its sockets, and the directory a client attaches to). Each server gets its own directory, so
    // two hosts never collide even when they serve the same chip id.
    std::filesystem::path server_directory;
    try {
        server_directory = SimulationConnector::allocate_server_directory();
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "sim_server start: could not allocate a server directory: {}", e.what());
        return 1;
    }

    // Daemonize with a single fork + setsid: the parent reports the child's pid and returns to the
    // shell; the child detaches into its own session and serves in the background.
    const pid_t pid = ::fork();
    if (pid < 0) {
        log_error(tt::LogUMD, "sim_server start: fork failed: {}", std::strerror(errno));
        return 1;
    }
    if (pid > 0) {
        std::cout << fmt::format(
            "started simulation host pid {} (serving {} in {})\n", pid, simulator_path, server_directory.string());
        return 0;
    }

    // --- daemon ---
    ::setsid();
    if (::chdir("/") != 0) { /* best effort; not fatal */
    }
    redirect_stdio_to_log();

    if (::pipe(g_stop_pipe) != 0) {
        _exit(1);
    }
    // Make the write end non-blocking so request_stop() -- reached from a signal handler and from a
    // serving thread -- can never block. One pending byte is all the main thread needs, so if the
    // pipe is already full the write harmlessly fails (a wakeup is already queued). The read end
    // stays blocking: the main thread blocks on it until stopped.
    const int flags = ::fcntl(g_stop_pipe[1], F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(g_stop_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);

    try {
        ClusterOptions options;
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = simulator_path;
        options.simulator_server_directory = server_directory;  // the directory claimed above
        Cluster cluster(options);

        // Opt into over-socket shutdown: a client's SHUTDOWN request signals this daemon's main
        // thread to stop; dropping the Cluster below then tears everything down gracefully.
        for (const tt::ChipId chip_id : cluster.get_target_device_ids()) {
            if (auto* device = dynamic_cast<SimulationTTDevice*>(cluster.get_tt_device(chip_id))) {
                device->set_shutdown_handler(request_stop);
            }
        }

        log_info(
            tt::LogUMD,
            "Simulation host up (from {}); send SHUTDOWN over the socket or SIGTERM to stop.",
            simulator_path);

        // Block until stopped (SHUTDOWN request or signal), tolerating EINTR.
        char byte = 0;
        ssize_t n = 0;
        do {
            n = ::read(g_stop_pipe[0], &byte, 1);
        } while (n < 0 && errno == EINTR);

        log_info(tt::LogUMD, "Simulation host shutting down; tearing down cluster and closing client connections.");
        // Cluster destructor (end of scope) performs the graceful teardown.
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Simulation host failed: {}", e.what());
        _exit(1);
    }
    _exit(0);
}

int cmd_list() {
    const std::vector<SimulationServerInfo> servers = SimulationConnector::list_servers();
    if (servers.empty()) {
        std::cout << "No simulation servers running.\n";
        return 0;
    }
    std::cout << fmt::format("{:<8} {:<6} {:<12} {:<10} {}\n", "SERVER", "CHIP", "STATE", "ARCH", "SOCKET");
    for (const SimulationServerInfo& server : servers) {
        if (server.sockets.empty()) {
            // Directory claimed but not yet (or no longer) serving any chip -- e.g. a host that is
            // still coming up, or one that died and left its directory behind.
            std::cout << fmt::format(
                "{:<8} {:<6} {:<12} {:<10} {}\n", server.index, "-", "empty", "-", server.directory.string());
            continue;
        }
        for (const auto& [chip_id, socket_path] : server.sockets) {
            std::string state = "unreachable";
            std::string arch = "-";
            try {
                SimulationClient client(socket_path);
                const SimulationServerDeviceInfo info =
                    fetch_device_info_from_host(client);  // attaches + GET_DEVICE_INFO
                state = "live";
                arch = fmt::format(
                    "{}/{}",
                    tt::arch_to_str(static_cast<tt::ARCH>(info.arch)),
                    info.backend_type == SimulationBackendType::TTSim ? "ttsim" : "rtl");
            } catch (const std::exception&) {
                // Socket file present but no live host answering -> stale/unreachable.
            }
            std::cout << fmt::format(
                "{:<8} {:<6} {:<12} {:<10} {}\n", server.index, chip_id, state, arch, socket_path.string());
        }
    }
    return 0;
}

int cmd_kill(int server_index) {
    const std::vector<SimulationServerInfo> servers = SimulationConnector::list_servers();
    const auto it = std::find_if(servers.begin(), servers.end(), [server_index](const SimulationServerInfo& s) {
        return s.index == server_index;
    });
    if (it == servers.end()) {
        log_error(tt::LogUMD, "No simulation server {} found.", server_index);
        return 1;
    }
    if (it->sockets.empty()) {
        log_error(tt::LogUMD, "Simulation server {} has no live socket to send shutdown to.", server_index);
        return 1;
    }
    // The whole host is one process: a SHUTDOWN on any of its chip sockets stops it and closes the
    // rest, so it is enough to send to the first socket.
    const std::filesystem::path& socket_path = it->sockets.begin()->second;
    try {
        SimulationClient client(socket_path);
        client.attach();
        SimulationServerRequest request;
        request.command = SimulationServerCommand::Shutdown;
        const SimulationServerResponse response = decode_response(client.transact(encode(request)));
        if (response.status != 0) {
            log_error(tt::LogUMD, "Server {} did not acknowledge shutdown (status {}).", server_index, response.status);
            return 1;
        }
        std::cout << fmt::format("Requested shutdown of simulation server {}.\n", server_index);
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "Failed to reach simulation server {}: {}", server_index, e.what());
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    cxxopts::Options options("sim_server", "Manage simulation host processes (start / list / kill).");
    options.add_options()("command", "start | list | kill", cxxopts::value<std::string>())(
        "arg", "start: <simulator.so | rtl-dir>;  kill: <server>", cxxopts::value<std::string>())(
        "h,help", "Print usage");
    options.parse_positional({"command", "arg"});
    options.positional_help("<command> [arg]");

    const auto result = options.parse(argc, argv);

    if (result.count("help") != 0u || result.count("command") == 0u) {
        std::cout << options.help() << std::endl;
        return result.count("help") != 0u ? 0 : 2;
    }

    const std::string command = result["command"].as<std::string>();

    if (command == "list") {
        return cmd_list();
    }
    if (command == "start") {
        if (result.count("arg") == 0u) {
            log_error(tt::LogUMD, "sim_server start requires a <simulator.so | rtl-dir> argument.");
            return 2;
        }
        return cmd_start(result["arg"].as<std::string>());
    }
    if (command == "kill") {
        if (result.count("arg") == 0u) {
            log_error(tt::LogUMD, "sim_server kill requires a <chip_id> argument.");
            return 2;
        }
        const std::string arg = result["arg"].as<std::string>();
        try {
            return cmd_kill(std::stoi(arg));
        } catch (const std::exception&) {
            log_error(tt::LogUMD, "Invalid server index '{}'.", arg);
            return 2;
        }
    }

    log_error(tt::LogUMD, "Unknown command '{}'.", command);
    std::cout << options.help() << std::endl;
    return 2;
}
