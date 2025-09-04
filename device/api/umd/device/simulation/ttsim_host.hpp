// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/tt_xy_pair.h"

#define NNG_SOCKET_PREFIX "ipc:///tmp/"

typedef struct nng_socket_s nng_socket;
typedef struct nng_dialer_s nng_dialer;

namespace tt::umd {

class TTSimHost {
public:
    TTSimHost();
    ~TTSimHost();

    void start_host();
    void send_to_device(uint8_t *buf, size_t buf_size);
    size_t recv_from_device(void **data_ptr);

private:
    std::unique_ptr<nng_socket> host_socket;
    std::unique_ptr<nng_dialer> host_dialer;
};

}  // namespace tt::umd
