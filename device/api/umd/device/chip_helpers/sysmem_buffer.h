/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

namespace tt::umd {

class SysmemBuffer {
public:
    SysmemBuffer(void* buffer_va, uint32_t buffer_size, uint64_t device_io_addr);
    ~SysmemBuffer() = default;

    void* get_buffer_va() const { return buffer_va; }

    uint32_t get_buffer_size() const { return buffer_size; }

    uint64_t get_device_io_addr() const { return device_io_addr; }

private:
    // Virtual address in process addr space.
    void* buffer_va;

    uint32_t buffer_size;

    // Address that is used on the system bus to access the buffer.
    uint64_t device_io_addr;
};

}  // namespace tt::umd
