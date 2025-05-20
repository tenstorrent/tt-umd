/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "ioctl.h"

namespace tt::umd {

class TlbHandle {
public:
    TlbHandle(uint32_t fd, size_t size);

    ~TlbHandle() noexcept;

    void configure(const tenstorrent_noc_tlb_config& new_config);

    uint8_t* get_base();
    size_t get_size() const;
    const tenstorrent_noc_tlb_config& get_config() const;

private:
    void free_tlb() noexcept;

    int tlb_id;
    uint8_t* tlb_base;
    size_t tlb_size;
    tenstorrent_noc_tlb_config tlb_config{};
    uint32_t fd;
};
}  // namespace tt::umd
