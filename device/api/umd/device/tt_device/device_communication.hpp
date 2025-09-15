/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TTDeviceCommunication {
public:
    virtual ~TTDeviceCommunication() = default;

    virtual void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;
    virtual void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;

    virtual void wait_for_non_mmio_flush() = 0;
    virtual bool is_remote() = 0;
};

}  // namespace tt::umd
