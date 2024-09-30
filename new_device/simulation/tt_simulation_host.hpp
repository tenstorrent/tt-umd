// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <vector>
#include <cstdint>

#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>

#include "new_device/common_types.h"

#define NNG_SOCKET_PREFIX "ipc:///tmp/"

class tt_SimulationHost {
public:
    tt_SimulationHost();
    ~tt_SimulationHost();

    void start_host();
    void send_to_device(uint8_t *buf, size_t buf_size);
    size_t recv_from_device(void **data_ptr);
private:
    nng_socket host_socket;
    nng_dialer host_dialer;
};
