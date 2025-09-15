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
    virtual void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;
    virtual void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;

    virtual void write_block(uint64_t byte_addr, uint64_t num_bytes, const uint8_t* buffer_addr) = 0;
    virtual void read_block(uint64_t byte_addr, uint64_t num_bytes, uint8_t* buffer_addr) = 0;

    virtual void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) = 0;
    virtual void write_regs(uint32_t byte_addr, uint32_t word_len, const void* data) = 0;
    virtual void read_regs(uint32_t byte_addr, uint32_t word_len, void* data) = 0;

    virtual void wait_for_non_mmio_flush() = 0;
    virtual bool is_remote() = 0;
};

}  // namespace tt::umd
