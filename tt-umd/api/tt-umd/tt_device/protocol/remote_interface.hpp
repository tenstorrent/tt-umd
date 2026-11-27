/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "tt-umd/types/noc_id.hpp"

namespace tt::umd {

class RemoteCommunication;

/**
 * RemoteInterface defines remote/Ethernet-specific operations beyond the basic DeviceProtocol.
 */
class RemoteInterface {
public:
    virtual ~RemoteInterface() = default;

    virtual RemoteCommunication* get_remote_communication() = 0;
    virtual void wait_for_non_mmio_flush(NocId noc_id = NocId::DEFAULT_NOC) = 0;
};

}  // namespace tt::umd
