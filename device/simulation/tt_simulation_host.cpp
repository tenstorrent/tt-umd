// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_simulation_host.hpp"

#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <tt-logger/tt-logger.hpp>
#include <typeinfo>
#include <random>
#include <unistd.h>

#include "assert.hpp"

namespace tt::umd {

tt_SimulationHost::tt_SimulationHost() {
    // Initialize socket and listener
    host_socket = std::make_unique<nng_socket>();
    host_listener = std::make_unique<nng_listener>();

    // Check if NNG_SOCKET_ADDR is already set
    const char *existing_socket_addr = std::getenv("NNG_SOCKET_ADDR");
    std::string nng_socket_addr_str;
    
    if (existing_socket_addr) {
        nng_socket_addr_str = existing_socket_addr;
        log_info(tt::LogEmulationDriver, "Using existing NNG_SOCKET_ADDR: {}", nng_socket_addr_str);
    } else {
        // Generate socket address with hostname and random port
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) != 0) {
            strcpy(hostname, "localhost");
        }
        
        // Generate random port in range 50000-59999
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(50000, 59999);
        int port = dis(gen);
        
        std::ostringstream ss;
        ss << "tcp://" << hostname << ":" << port;
        nng_socket_addr_str = ss.str();
        
        // Export the address for client to use
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

tt_SimulationHost::~tt_SimulationHost() {
    nng_listener_close(*host_listener);
    nng_close(*host_socket);
}

void tt_SimulationHost::start_host() {
    // Start listening for connections from client
    int rv = nng_listener_start(*host_listener, 0);
    if (rv != 0) {
        log_error(tt::LogEmulationDriver, "Failed to start listener: {}", nng_strerror(rv));
        return;
    }
    
    log_info(tt::LogEmulationDriver, "Server started, waiting for client to connect...");

}

void tt_SimulationHost::send_to_device(uint8_t *buf, size_t buf_size) {
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

size_t tt_SimulationHost::recv_from_device(void **data_ptr) {
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
