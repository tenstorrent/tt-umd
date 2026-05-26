// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class SystemMemoryBuffer {
public:
    virtual ~SystemMemoryBuffer() = default;
    virtual void *get_ptr() const = 0;
    virtual uint64_t get_iova() const = 0;
    virtual size_t get_size() const = 0;
};

class SystemMemoryAllocator {
public:
    virtual ~SystemMemoryAllocator() = default;
    virtual std::unique_ptr<SystemMemoryBuffer> allocate(size_t size) = 0;
};

}  // namespace tt::umd
