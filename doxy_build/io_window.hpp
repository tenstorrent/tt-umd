// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class IoWindow {
public:
    virtual ~IoWindow() = default;
    virtual void write_block(uint64_t offset, const void *data, size_t size) = 0;
    virtual void read_block(uint64_t offset, void *data, size_t size) = 0;
    virtual void write32(uint64_t offset, uint32_t value) = 0;
    virtual uint32_t read32(uint64_t offset) = 0;
    virtual void configure(const TargetIoWindowConfig &config) = 0;
    virtual size_t get_size() const = 0;
};

}  // namespace tt::umd
