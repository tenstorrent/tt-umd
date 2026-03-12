// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {
class TTDevice;

class CoordIO {
public:
    CoordIO(TTDevice* tt_device, const SocDescriptor& soc_descriptor);
    void read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, uint32_t size);
    void write_to_device(const void* mem_ptr, CoreCoord core, uint64_t addr, uint32_t size);
    void noc_multicast_write(void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr);

private:
    TTDevice* tt_device_ = nullptr;
    SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
