// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
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
    void start_simulator(const std::filesystem::path &simulator_directory);
    void send_to_device(uint8_t *buf, size_t buf_size);
    size_t recv_from_device(void **data_ptr);

private:
    static constexpr int SEND_TIMEOUT_MS = 30000;  // 30 seconds
    static constexpr int RECV_TIMEOUT_MS = 60000;  // 60 seconds

    std::unique_ptr<nng_socket> host_socket;
    std::unique_ptr<nng_listener> host_listener;
    pid_t child_process_pid = -1;

    bool is_child_process_alive() const;
};

}  // namespace tt::umd
