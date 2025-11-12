// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_host.hpp"

#include <netinet/in.h>
#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <uv.h>

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <tt-logger/tt-logger.hpp>
#include <typeinfo>

#include "assert.hpp"

namespace tt::umd {

bool is_port_free(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bool free = (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0);
    close(sock);
    return free;
}

SimulationHost::SimulationHost() {
    // Initialize socket and listener
    host_socket = std::make_unique<nng_socket>();
    host_listener = std::make_unique<nng_listener>();
}

void SimulationHost::init() {
    // Check if NNG_SOCKET_LOCAL_PORT is set
    const char *local_socket_port_str = std::getenv("NNG_SOCKET_LOCAL_PORT");
    std::string nng_socket_addr_str;

    // Generate socket address with hostname and random port
    char hostname[256];
    if (std::getenv("TT_SIMULATOR_LOCALHOST") || gethostname(hostname, sizeof(hostname)) != 0) {
        strcpy(hostname, "localhost");
    }

    int port;

    if (local_socket_port_str) {
        port = atoi(local_socket_port_str);
        log_info(tt::LogEmulationDriver, "Using specified NNG_SOCKET_LOCAL_PORT: {}", port);
    } else {
        // Generate random port in range 50000-59999
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(50000, 59999);

        do {
            port = dis(gen);
        } while (!is_port_free(port));
        log_info(tt::LogEmulationDriver, "Using generated port: {}", port);
    }

    std::ostringstream ss;
    ss << "tcp://" << hostname << ":" << port;
    nng_socket_addr_str = ss.str();

    // Export the address for client to use
    if (std::getenv("NNG_SOCKET_ADDR") == nullptr) {
        setenv("NNG_SOCKET_ADDR", nng_socket_addr_str.c_str(), 1);
        log_info(tt::LogEmulationDriver, "Generated NNG_SOCKET_ADDR: {}", nng_socket_addr_str);
    }

    const char *nng_socket_addr = nng_socket_addr_str.c_str();

    // Open socket and create listener (server mode)
    log_info(tt::LogEmulationDriver, "Listening on: {}", nng_socket_addr);
    nng_pair1_open(host_socket.get());
    int rv = nng_listener_create(host_listener.get(), *host_socket, nng_socket_addr);
    TT_ASSERT(rv == 0, "Failed to create listener: {} {}", nng_strerror(rv), nng_socket_addr);
}

SimulationHost::~SimulationHost() {
    nng_listener_close(*host_listener);
    nng_close(*host_socket);
}

void SimulationHost::start_host() {
    // Start listening for connections from client
    int rv = nng_listener_start(*host_listener, 0);
    if (rv != 0) {
        log_error(tt::LogEmulationDriver, "Failed to start listener: {}", nng_strerror(rv));
        return;
    }

    log_info(tt::LogEmulationDriver, "Server started, waiting for client to connect...");
}

void SimulationHost::send_to_device(uint8_t *buf, size_t buf_size) {
    int rv;
    log_debug(tt::LogEmulationDriver, "Sending messsage to remote..");

    // Set timeout for send operations
    nng_duration timeout = SEND_TIMEOUT_MS;
    rv = nng_socket_set_ms(*host_socket, NNG_OPT_SENDTIMEO, timeout);
    if (rv != 0) {
        log_info(tt::LogEmulationDriver, "Failed to set send timeout: {}", nng_strerror(rv));
    }

    int attempt = 0;
    while (true) {
        void *msg = nng_alloc(buf_size);
        std::memcpy(msg, buf, buf_size);

        rv = nng_send(*host_socket, msg, buf_size, NNG_FLAG_ALLOC);

        if (rv == 0) {
            log_debug(tt::LogEmulationDriver, "Message sent successfully{}", attempt > 0 ? " after retry" : "");
            return;
        }

        if (rv == NNG_ETIMEDOUT) {
            // Check if child process is still alive on timeout
            if (!is_child_process_alive()) {
                TT_THROW("Send timeout: Simulator child process has terminated unexpectedly");
            } else {
                ++attempt;
                log_info(
                    tt::LogEmulationDriver,
                    "Send timeout after {}ms, retrying... (attempt {})",
                    SEND_TIMEOUT_MS,
                    attempt);
                continue;  // Retry forever while process is alive
            }
        } else {
            // Other errors - don't retry, just throw
            log_info(tt::LogEmulationDriver, "Failed to send message to remote: {}", nng_strerror(rv));
            TT_THROW("Failed to send message to remote: {}", nng_strerror(rv));
        }
    }
}

size_t SimulationHost::recv_from_device(void **data_ptr) {
    int rv;
    size_t data_size = 0;
    log_debug(tt::LogEmulationDriver, "Receiving messsage from remote..");

    // Set timeout for receive operations
    nng_duration timeout = RECV_TIMEOUT_MS;
    rv = nng_socket_set_ms(*host_socket, NNG_OPT_RECVTIMEO, timeout);
    if (rv != 0) {
        log_info(tt::LogEmulationDriver, "Failed to set receive timeout: {}", nng_strerror(rv));
    }

    int attempt = 0;
    while (true) {
        rv = nng_recv(*host_socket, data_ptr, &data_size, NNG_FLAG_ALLOC);

        if (rv == 0) {
            log_debug(tt::LogEmulationDriver, "Message received successfully{}", attempt > 0 ? " after retry" : "");
            return data_size;
        }

        if (rv == NNG_ETIMEDOUT) {
            // Check if child process is still alive on timeout
            if (!is_child_process_alive()) {
                TT_THROW("Receive timeout: Simulator child process has terminated unexpectedly");
            } else {
                ++attempt;
                log_info(
                    tt::LogEmulationDriver,
                    "Receive timeout after {}ms, retrying... (attempt {})",
                    RECV_TIMEOUT_MS,
                    attempt);
                continue;  // Retry forever while process is alive
            }
        } else {
            // Other errors - don't retry, just throw
            log_info(tt::LogEmulationDriver, "Failed to receive message from remote: {}", nng_strerror(rv));
            TT_THROW("Failed to receive message from remote: {}", nng_strerror(rv));
        }
    }
}

void SimulationHost::start_simulator(const std::filesystem::path &simulator_directory) {
    // Start simulator process
    uv_loop_t *loop = uv_default_loop();
    std::string simulator_path_string = simulator_directory / "run.sh";
    if (!std::filesystem::exists(simulator_path_string)) {
        TT_THROW("Simulator binary not found at: ", simulator_path_string);
    }

    uv_stdio_container_t child_stdio[3];
    child_stdio[0].flags = UV_IGNORE;
    child_stdio[1].flags = UV_INHERIT_FD;
    child_stdio[1].data.fd = 1;
    child_stdio[2].flags = UV_INHERIT_FD;
    child_stdio[2].data.fd = 2;

    uv_process_options_t child_options = {0};
    child_options.file = simulator_path_string.c_str();
    child_options.flags = UV_PROCESS_DETACHED;
    child_options.stdio_count = 3;
    child_options.stdio = child_stdio;

    uv_process_t child_p;
    int rv = uv_spawn(loop, &child_p, &child_options);
    if (rv) {
        TT_THROW("Failed to spawn simulator process: ", uv_strerror(rv));
    } else {
        child_process_pid = child_p.pid;
        log_info(tt::LogEmulationDriver, "Simulator process spawned with PID: {}", child_process_pid);
    }

    uv_unref(reinterpret_cast<uv_handle_t *>(&child_p));
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
}

bool SimulationHost::is_child_process_alive() const {
    if (child_process_pid == -1) {
        log_info(tt::LogEmulationDriver, "No child process PID set, assuming alive");
        return true;  // No child process to check, assume alive
    }

    // First check if process exists at all
    int result = kill(child_process_pid, 0);
    if (result != 0) {
        if (errno == ESRCH) {
            log_debug(tt::LogEmulationDriver, "Child process {} is dead (ESRCH)", child_process_pid);
            return false;  // Process doesn't exist
        } else {
            log_debug(
                tt::LogEmulationDriver,
                "Cannot check child process {} status: {} - assuming alive",
                child_process_pid,
                strerror(errno));
            return true;
        }
    }

    // Process exists, but check if it's a zombie by reading /proc/PID/stat
    std::string stat_path = "/proc/" + std::to_string(child_process_pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        log_debug(tt::LogEmulationDriver, "Cannot open {}, assuming process {} is alive", stat_path, child_process_pid);
        return true;
    }

    std::string line;
    if (std::getline(stat_file, line)) {
        // Parse the stat line: PID (comm) state ...
        // Find the state field (3rd field after the closing parenthesis)
        size_t paren_pos = line.rfind(')');
        if (paren_pos != std::string::npos && paren_pos + 2 < line.length()) {
            char state = line[paren_pos + 2];  // Skip ") " to get state
            if (state == 'Z') {
                log_debug(tt::LogEmulationDriver, "Child process {} is zombie (state: Z)", child_process_pid);
                return false;  // Zombie process is effectively dead
            }
            log_debug(tt::LogEmulationDriver, "Child process {} is alive (state: {})", child_process_pid, state);
            return true;
        }
    }

    // If we can't parse the state, assume dead (fail fast)
    log_info(tt::LogEmulationDriver, "Cannot parse state for process {} - assuming dead for safety", child_process_pid);
    return false;
}

}  // namespace tt::umd
