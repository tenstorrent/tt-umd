// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// sim_server: run and manage long-running simulation hosts, so other UMD processes can attach to a
// simulation as clients over its per-chip socket.
//
//   sim_server start [-d] <simulator.so | rtl-dir>  -- serve a simulation until stopped; -d detaches
//                                                      it and returns once it is serving.
//   sim_server list                                 -- show the open servers and their chips.
//   sim_server kill <server>                        -- ask a server to shut down, over its socket.
//   sim_server prune                                -- remove the directories of servers that are gone.
//
// kill goes over the socket rather than by signal because the socket is world-writable, so it also
// works across users, which a signal would not.

#include <fcntl.h>
#include <fmt/format.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/cluster.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/simulation/simulation_device_identity.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

namespace {

// Row layout shared by the `list` header and its rows.
constexpr const char* LIST_ROW = "{:<8} {:<6} {:<12} {:<16} {}\n";

// Raised by a SHUTDOWN request (on a serving thread) or by SIGINT/SIGTERM; polled by cmd_start.
// volatile sig_atomic_t is the only thing a signal handler may touch.
volatile std::sig_atomic_t stop_requested = 0;

void request_stop() { stop_requested = 1; }

// Where a detached host's output goes: one log per server, beside the server directories and named
// after the one it belongs to, so a log can be found from a `list` row and removed with its server.
std::filesystem::path log_path_for(const std::filesystem::path& server_directory) {
    return std::filesystem::temp_directory_path() /
           fmt::format("sim_server-{}.log", server_directory.filename().string());
}

// Which servers are past saving is the connector's judgement, and it clears up after them whenever
// it enumerates; their logs are this tool's own doing, so they go at the same moment. Every command
// that enumerates calls this first, so the two halves cannot drift apart.
std::vector<SimulationServerInfo> clear_up_dead_servers() {
    const std::vector<SimulationServerInfo> pruned = SimulationConnector::prune_dead_servers();
    for (const SimulationServerInfo& server : pruned) {
        std::error_code ec;
        std::filesystem::remove(log_path_for(server.directory), ec);
    }
    return pruned;
}

// Asks the host on this socket who it is. Returns "<arch>/<backend>", or "" if nothing answers.
std::string probe_socket(const std::filesystem::path& socket_path) {
    try {
        SimulationClient client(socket_path);
        const SimulationServerDeviceInfo info = fetch_device_info_from_host(client);  // attaches + GET_DEVICE_INFO
        return fmt::format(
            "{}/{}",
            tt::arch_to_str(static_cast<tt::ARCH>(info.arch)),
            info.backend_type == SimulationBackendType::TTSIM ? "ttsim" : "rtl");
    } catch (const std::exception&) {
        return "";  // socket file present, but no live host answering
    }
}

// Points everything the host prints at its own log and detaches stdin, so a detached host neither
// writes to nor reads from the terminal that started it. Line-buffered: a log that only materialises
// when the process exits is no use for diagnosing a host that is stuck.
//
// O_NOFOLLOW, because the log path is derived from the server index and so is predictable, and the
// temp directory it sits in is world-writable: following a symlink planted there would truncate
// whatever it names, with this process's privileges. Owner-only for the same reason. Refusing to
// start beats writing somewhere else.
void redirect_standard_streams(const std::filesystem::path& log) {
    const int null_fd = open("/dev/null", O_RDONLY);
    const int log_fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    const bool redirected = null_fd >= 0 && log_fd >= 0 && dup2(null_fd, STDIN_FILENO) >= 0 &&
                            dup2(log_fd, STDOUT_FILENO) >= 0 && dup2(log_fd, STDERR_FILENO) >= 0;
    const int failure = errno;
    close(null_fd);
    close(log_fd);
    if (!redirected) {
        throw std::runtime_error(fmt::format("Could not redirect output to {}: {}", log.string(), strerror(failure)));
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
}

int cmd_start(const std::filesystem::path& simulator_path, bool detach) {
    // Claim the directory up front rather than letting Cluster pick one: it names the log, it is
    // reported on stdout in a form a caller can parse (Cluster logs it too, but a log line is not an
    // interface), and failing to claim one stays visible in the foreground.
    const std::filesystem::path server_directory = SimulationConnector::allocate_server_directory();

    // Write end of the readiness pipe once detached; -1 while running in the foreground. The child
    // signals through it as soon as the sockets are serving, and its close-on-exit is what tells the
    // parent that a host died on the way up -- so neither side polls, and there is no startup
    // timeout to guess at (an RTL build can take a while to come up, and killing it would be worse
    // than waiting).
    int ready_fd = -1;
    if (detach) {
        const std::filesystem::path log = log_path_for(server_directory);
        int ready_pipe[2];
        if (pipe(ready_pipe) != 0) {
            throw std::runtime_error(fmt::format("Could not create the readiness pipe: {}", strerror(errno)));
        }
        const pid_t pid = fork();
        if (pid < 0) {
            throw std::runtime_error(fmt::format("Could not fork a host process: {}", strerror(errno)));
        }
        if (pid > 0) {
            close(ready_pipe[1]);
            char ready = 0;
            const ssize_t received = read(ready_pipe[0], &ready, 1);
            close(ready_pipe[0]);
            if (received == 1) {
                std::cout << fmt::format(
                    "sim_server up: pid {}, serving {}, log {}\n", pid, server_directory.string(), log.string());
                return 0;
            }
            // Nothing arrived, so the child exited before it served. Reap it and point at the log,
            // which holds whatever it managed to say.
            int status = 0;
            waitpid(pid, &status, 0);
            log_error(tt::LogUMD, "sim_server exited during startup; see {}.", log.string());
            return 1;
        }
        // Child: leave the starting shell's session, so the host survives the terminal closing and
        // is not hit by its Ctrl-C.
        close(ready_pipe[0]);
        setsid();
        redirect_standard_streams(log);
        ready_fd = ready_pipe[1];
    }

    std::signal(SIGINT, [](int) { request_stop(); });
    std::signal(SIGTERM, [](int) { request_stop(); });

    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path;
    // A simulator that ships a cluster_descriptor.yaml is enumerated from it, and leaving
    // target_devices empty then means "every chip in it". Without one, Cluster falls back to a mock
    // descriptor built *from* target_devices -- so leaving it empty there yields a Cluster with zero
    // chips that serves zero sockets. Name chip 0 in that case, and only that case.
    if (!std::filesystem::exists(SimulationChip::get_cluster_descriptor_path_from_simulator_path(simulator_path))) {
        options.target_devices = {0};
    }
    // Serving the per-chip sockets is opt-in, and it is this tool's whole job.
    options.serve_simulation_devices_over_sockets = true;
    options.simulation_shutdown_handler = request_stop;
    options.simulator_server_directory = server_directory;

    Cluster cluster(options);

    // Printed only once the sockets are actually serving, so a caller can treat this line -- and not
    // the process merely existing -- as the signal that clients may attach.
    std::cout << "server directory: " << server_directory.string() << std::endl;
    if (ready_fd >= 0) {
        const char ready = 1;
        [[maybe_unused]] const ssize_t sent = write(ready_fd, &ready, 1);
        close(ready_fd);
    }

    log_info(tt::LogUMD, "Simulation host up. Stop it with Ctrl-C, SIGTERM, or `sim_server kill`.");
    while (stop_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log_info(tt::LogUMD, "Shutting down; closing client connections.");
    return 0;  // ~Cluster tears the host down gracefully.
}

int cmd_list() {
    clear_up_dead_servers();
    const std::vector<SimulationServerInfo> servers = SimulationConnector::list_servers();
    if (servers.empty()) {
        std::cout << "No simulation servers running.\n";
        return 0;
    }
    std::cout << fmt::format(LIST_ROW, "SERVER", "CHIP", "STATE", "ARCH", "SOCKET");
    for (const SimulationServerInfo& server : servers) {
        // A server that is gone is already left out of the listing, so a directory with no sockets
        // here is a host still coming up.
        if (server.sockets.empty()) {
            std::cout << fmt::format(LIST_ROW, server.index, "-", "empty", "-", server.directory.string());
            continue;
        }
        for (const auto& [chip_id, socket_path] : server.sockets) {
            // Reachable only if the host died between the listing and this probe.
            const std::string arch = probe_socket(socket_path);
            const bool live = !arch.empty();
            std::cout << fmt::format(
                LIST_ROW,
                server.index,
                chip_id,
                live ? "live" : "unreachable",
                live ? arch : "-",
                socket_path.string());
        }
    }
    return 0;
}

int cmd_kill(int server_index) {
    clear_up_dead_servers();
    const std::vector<SimulationServerInfo> servers = SimulationConnector::list_servers();
    const auto it = std::find_if(servers.begin(), servers.end(), [server_index](const SimulationServerInfo& server) {
        return server.index == server_index;
    });
    if (it == servers.end() || it->sockets.empty()) {
        log_error(
            tt::LogUMD, "No simulation server {} with a socket to shut down; see `sim_server list`.", server_index);
        return 1;
    }

    // The host is one process, so a SHUTDOWN on any of its chip sockets stops it and closes the rest.
    SimulationClient client(it->sockets.begin()->second);
    client.attach();
    SimulationServerRequest request;
    request.command = SimulationServerCommand::SHUTDOWN;
    const SimulationServerResponse response = decode_response(client.transact(encode(request)));
    if (response.status != 0) {
        log_error(tt::LogUMD, "Server {} did not acknowledge shutdown (status {}).", server_index, response.status);
        return 1;
    }
    std::cout << fmt::format("Requested shutdown of simulation server {}.\n", server_index);
    return 0;
}

int cmd_prune() {
    const std::vector<SimulationServerInfo> pruned = clear_up_dead_servers();
    for (const SimulationServerInfo& server : pruned) {
        std::cout << fmt::format("pruned server {} ({})\n", server.index, server.directory.string());
    }
    if (pruned.empty()) {
        std::cout << "Nothing to prune.\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    cxxopts::Options options("sim_server", "Manage simulation hosts (start / list / kill / prune).");
    // clang-format off
    options.add_options()
        ("command",  "start | list | kill | prune",                            cxxopts::value<std::string>())
        ("arg",      "start: <simulator.so | rtl-dir>;  kill: <server index>", cxxopts::value<std::string>())
        ("d,detach", "start: serve in the background, and report where")
        ("h,help",   "Print usage");
    // clang-format on
    options.parse_positional({"command", "arg"});
    options.positional_help("<command> [arg]");

    const auto result = options.parse(argc, argv);
    if (result.count("help") || !result.count("command")) {
        std::cout << options.help() << std::endl;
        return result.count("help") ? 0 : 2;
    }

    const std::string command = result["command"].as<std::string>();
    const std::string arg = result.count("arg") ? result["arg"].as<std::string>() : "";
    if ((command == "start" || command == "kill") && arg.empty()) {
        log_error(tt::LogUMD, "sim_server {} requires an argument; see --help.", command);
        return 2;
    }

    try {
        if (command == "list") {
            return cmd_list();
        }
        if (command == "start") {
            return cmd_start(arg, result.count("detach") != 0);
        }
        if (command == "kill") {
            return cmd_kill(std::stoi(arg));
        }
        if (command == "prune") {
            return cmd_prune();
        }
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "sim_server {} failed: {}", command, e.what());
        return 1;
    }

    log_error(tt::LogUMD, "Unknown command '{}'; see --help.", command);
    return 2;
}
