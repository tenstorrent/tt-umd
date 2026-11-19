/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/types/io_window_config.hpp"

namespace tt::umd {

/**
 * @brief Creates I/O windows into a device's address space, for transports that can map one.
 */
class IoWindowFactory {
public:
    virtual ~IoWindowFactory() = default;

    /**
     * @brief Creates a caller-owned I/O window.
     * @param target Device-side target (core, address, NOC).
     * @param host Host-side properties (caching, size). A size of 0 leaves the size to the factory.
     * @param ordering Ordering of the window's transactions.
     * @return std::unique_ptr<IoWindow> Exclusively owned window handle.
     */
    virtual std::unique_ptr<IoWindow> create_io_window(
        TargetIoWindowConfig target, HostIoWindowConfig host, IoOrdering ordering) = 0;
};

}  // namespace tt::umd
