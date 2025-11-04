// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_host.hpp"

#include <netinet/in.h>
#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
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

    bool free = (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
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

    // Check if NNG_SOCKET_ADDR is already set
    const char *existing_socket_addr = std::getenv("NNG_SOCKET_ADDR");
    const char *nng_socket_addr;

    if (existing_socket_addr != nullptr) {
        // Use the existing environment variable
        nng_socket_addr = existing_socket_addr;
        log_info(tt::LogEmulationDriver, "Using existing NNG_SOCKET_ADDR: {}", nng_socket_addr);
    } else {
        // Generate new address and export it
        std::ostringstream ss;
        ss << "tcp://" << hostname << ":" << port;
        nng_socket_addr_str = ss.str();
        nng_socket_addr = nng_socket_addr_str.c_str();

        setenv("NNG_SOCKET_ADDR", nng_socket_addr, 1);
        log_info(tt::LogEmulationDriver, "Generated NNG_SOCKET_ADDR: {}", nng_socket_addr);
    }

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

    void *msg = nng_alloc(buf_size);
    std::memcpy(msg, buf, buf_size);

    rv = nng_send(*host_socket, msg, buf_size, NNG_FLAG_ALLOC);
    log_debug(tt::LogEmulationDriver, "Message sent.");
    if (rv != 0) {
        log_info(tt::LogEmulationDriver, "Failed to send message to remote: {}", nng_strerror(rv));
    }
}

size_t SimulationHost::recv_from_device(void **data_ptr) {
    int rv;
    size_t data_size;
    log_debug(tt::LogEmulationDriver, "Receiving messsage from remote..");
    rv = nng_recv(*host_socket, data_ptr, &data_size, NNG_FLAG_ALLOC);
    log_debug(tt::LogEmulationDriver, "Message received.");
    if (rv != 0) {
        log_info(tt::LogEmulationDriver, "Failed to receive message from remote: {}", nng_strerror(rv));
    }
    return data_size;
}

}  // namespace tt::umd
