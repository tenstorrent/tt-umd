// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "io_window.hpp"

namespace tt::umd {

class IoWindowFactory {
public:
    virtual ~IoWindowFactory() = default;
    virtual std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) = 0;
};

}  // namespace tt::umd
