/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/types/tlb.h"

namespace tt::umd {

class TlbHandle {
public:
    TlbHandle(uint32_t fd, size_t size);

    ~TlbHandle() noexcept;

    void configure(const tlb_data& new_config);

    uint8_t* get_base();
    size_t get_size() const;
    const tlb_data& get_config() const;

private:
    void free_tlb() noexcept;

    int tlb_id;
    uint8_t* tlb_base;
    size_t tlb_size;
    tlb_data tlb_config;
    uint32_t fd;
};
}  // namespace tt::umd
