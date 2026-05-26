// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class HangDetector {
public:
    virtual ~HangDetector() = default;
    std::optional<bool> is_bus_hung(uint32_t data_read = HANG_READ_VALUE);
    std::optional<bool> is_noc_hung(NocId noc);

protected:
    virtual uint32_t read_hang_check_reg_via_bar() = 0;
    virtual uint32_t read_hang_check_reg_via_noc(NocId noc) = 0;
};

}  // namespace tt::umd
