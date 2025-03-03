/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <unordered_map>

#include "umd/device/tt_xy_pair.h"
#include "umd/device/types/tlb.h"

namespace tt::umd {

class TlbWindow {
public:
    TlbWindow(TlbNocConfig tlb_noc_config, uint32_t uid, void* ptr);

    TlbNocConfig get_tlb_noc_config() const { return tlb_noc_config; }

    void* get_ptr() const { return ptr; }

    uint32_t get_uid() const { return uid; }

    ~TlbWindow();

private:
    TlbNocConfig tlb_noc_config;
    uint32_t uid;
    void* ptr;
};

}  // namespace tt::umd
