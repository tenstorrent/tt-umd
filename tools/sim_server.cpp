// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// sim_server: run and manage long-running simulation hosts, so other UMD processes can attach to a
// simulation as clients over its per-chip socket.
//
//   sim_server start <simulator.so | rtl-dir>  -- serve a simulation until stopped. Background it
//                                                 with `&` or `nohup ... &`.
//   sim_server list                            -- show the open servers and their chips.
//   sim_server kill <server>                   -- ask a server to shut down, over its socket.
//
// kill goes over the socket rather than by signal because the socket is world-writable, so it also
// works across users, which a signal would not.

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <string>
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

int cmd_start(const std::filesystem::path& simulator_path) {
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
    // Claim the directory here rather than letting Cluster pick one, so we can report it on stdout in
    // a form a caller can parse. Cluster logs it too, but a log line is not an interface.
    options.simulator_server_directory = SimulationConnector::allocate_server_directory();

    Cluster cluster(options);

    // Printed only once the sockets are actually serving, so a caller can treat this line -- and not
    // the process merely existing -- as the signal that clients may attach.
    std::cout << "server directory: " << options.simulator_server_directory.string() << std::endl;

    log_info(tt::LogUMD, "Simulation host up. Stop it with Ctrl-C, SIGTERM, or `sim_server kill`.");
    while (stop_requested == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log_info(tt::LogUMD, "Shutting down; closing client connections.");
    return 0;  // ~Cluster tears the host down gracefully.
}

int cmd_list() {
    const std::vector<SimulationServerInfo> servers = SimulationConnector::list_servers();
    if (servers.empty()) {
        std::cout << "No simulation servers running.\n";
        return 0;
    }
    std::cout << fmt::format(LIST_ROW, "SERVER", "CHIP", "STATE", "ARCH", "SOCKET");
    for (const SimulationServerInfo& server : servers) {
        // A directory with no sockets is a host still coming up, or one that died and left it behind.
        if (server.sockets.empty()) {
            std::cout << fmt::format(LIST_ROW, server.index, "-", "empty", "-", server.directory.string());
            continue;
        }
        for (const auto& [chip_id, socket_path] : server.sockets) {
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

}  // namespace

int main(int argc, char* argv[]) {
    cxxopts::Options options("sim_server", "Manage simulation hosts (start / list / kill).");
    // clang-format off
    options.add_options()
        ("command", "start | list | kill",                                    cxxopts::value<std::string>())
        ("arg",     "start: <simulator.so | rtl-dir>;  kill: <server index>", cxxopts::value<std::string>())
        ("h,help",  "Print usage");
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
            return cmd_start(arg);
        }
        if (command == "kill") {
            return cmd_kill(std::stoi(arg));
        }
    } catch (const std::exception& e) {
        log_error(tt::LogUMD, "sim_server {} failed: {}", command, e.what());
        return 1;
    }

    log_error(tt::LogUMD, "Unknown command '{}'; see --help.", command);
    return 2;
}
