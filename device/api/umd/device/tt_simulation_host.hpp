// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/tt_xy_pair.h"

#define NNG_SOCKET_PREFIX "tcp://soc-zebu-01:5556"

typedef struct nng_socket_s nng_socket;
typedef struct nng_listener_s nng_listener;

namespace tt::umd {

class tt_SimulationHost {
public:
    tt_SimulationHost();
    ~tt_SimulationHost();

    void start_host();
    void send_to_device(uint8_t *buf, size_t buf_size);
    size_t recv_from_device(void **data_ptr);

private:
    std::unique_ptr<nng_socket> host_socket;
    std::unique_ptr<nng_listener> host_listener;
};

}  // namespace tt::umd
