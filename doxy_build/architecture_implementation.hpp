// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class ArchitectureImplementation {
public:
    virtual ~ArchitectureImplementation() = default;
    virtual tt::ARCH get_architecture() const = 0;
    virtual uint32_t get_min_clock_freq() const = 0;
    virtual uint64_t get_arc_reset_unit_refclk_high_offset() const = 0;
    virtual uint64_t get_arc_reset_unit_refclk_low_offset() const = 0;
    virtual uint64_t get_tensix_soft_reset_addr() const = 0;
    virtual uint32_t get_soft_reset_reg_value(RiscType risc_type) const = 0;
    virtual uint32_t get_soft_reset_staggered_start() const = 0;
    virtual RiscType get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const = 0;
};

}  // namespace tt::umd
