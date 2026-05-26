// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class DeviceProtocol {
public:
    virtual ~DeviceProtocol() = default;
    virtual void write_data(const void *src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;
    virtual void read_data(void *dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;
    virtual void write_ctrl(const void *src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;
    virtual void read_ctrl(void *dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;
    [[nodiscard]] virtual bool write_to_core_range(
        const void *src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint32_t size, NocId noc_id) = 0;
};

}  // namespace tt::umd
