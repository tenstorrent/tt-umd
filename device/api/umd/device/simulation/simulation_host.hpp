// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/types/xy_pair.hpp"

typedef struct nng_socket_s nng_socket;
typedef struct nng_listener_s nng_listener;

namespace tt::umd {

class SimulationHost {
public:
    SimulationHost();
    ~SimulationHost();

    void init();
    void start_host();
    void send_to_device(uint8_t *buf, size_t buf_size);
    size_t recv_from_device(void **data_ptr);

private:
    std::unique_ptr<nng_socket> host_socket;
    std::unique_ptr<nng_listener> host_listener;
};

}  // namespace tt::umd
