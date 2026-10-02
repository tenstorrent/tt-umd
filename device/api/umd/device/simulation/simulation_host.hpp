// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/types/xy_pair.hpp"

typedef struct nng_socket_s nng_socket;
typedef struct nng_listener_s nng_listener;

namespace tt::umd {

class SimulationHost {
public:
    SimulationHost();
    ~SimulationHost();

    /**
     * Create the listener. With an empty @p suffix, reads NNG_SOCKET_LOCAL_PORT and exports
     * NNG_SOCKET_ADDR; otherwise reads NNG_SOCKET_LOCAL_PORT_<suffix> and exports
     * NNG_SOCKET_ADDR_<suffix>. @p suffix must be [A-Za-z0-9_]+, so run.sh can read the variables.
     */
    void init(const std::string &suffix = "");
    void start_host();
    void send_to_device(uint8_t *buf, size_t buf_size);
    size_t recv_from_device(void **data_ptr);
    size_t recv_from_device(void **data_ptr, int timeout_ms);

private:
    std::unique_ptr<nng_socket> host_socket;
    std::unique_ptr<nng_listener> host_listener;
};

}  // namespace tt::umd
