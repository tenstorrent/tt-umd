// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class PcieInterface {
public:
    virtual ~PcieInterface() = default;
    virtual void bar_write32(uint32_t addr, uint32_t data) = 0;
    virtual uint32_t bar_read32(uint32_t addr) = 0;
    virtual int get_numa_node() const = 0;
};

}  // namespace tt::umd
