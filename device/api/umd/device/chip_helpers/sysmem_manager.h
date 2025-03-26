/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

class SysmemManager {
public:
    SysmemManager(TTDevice* tt_device);
    void write_to_sysmem(const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel);

    void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size);

    // hugepage and iommu init to be defined here.

private:
    TTDevice* tt_device_;
};

}  // namespace tt::umd
