/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class DeviceProtocol {
public:
    virtual ~DeviceProtocol() = default;

    virtual void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;
    virtual void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;

    virtual void write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) = 0;
    virtual void read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) = 0;

    virtual void wait_for_non_mmio_flush() = 0;
    virtual bool is_remote() = 0;

    virtual void set_noc_translation_enabled(bool noc_translation_enabled) = 0;
};

}  // namespace tt::umd
