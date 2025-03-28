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

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size);
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size);

    // hugepage and iommu init to be defined here.

private:
    TTDevice* tt_device_;
};

}  // namespace tt::umd
