// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "remote_communication.hpp"

namespace tt::umd {

class RemoteInterface {
public:
    virtual ~RemoteInterface() = default;
    virtual RemoteCommunication *get_remote_communication() = 0;
};

}  // namespace tt::umd
