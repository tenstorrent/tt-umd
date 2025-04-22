/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "umd/device/tt_device/tlb_handle.h"

namespace tt::umd {

class TlbWindow {
public:
    TlbWindow(std::unique_ptr<TlbHandle> handle);

    void write32(uint64_t offset, uint32_t value);

    uint32_t read32(uint64_t offset);

    void write_register(uint64_t offset, uint32_t value);

    uint32_t read_register(uint64_t offset);

    void write_block(uint64_t offset, const void* data, size_t size);

    void read_block(uint64_t offset, void* data, size_t size);

    TlbHandle& handle_ref();

    size_t get_size() const;

private:
    void validate(uint64_t offset, size_t size) const;

    std::unique_ptr<TlbHandle> handle;
};

}  // namespace tt::umd
