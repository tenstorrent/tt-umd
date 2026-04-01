// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/chip_helpers/tt_sim_tlb_manager.hpp"

namespace tt::umd{

// Can't inherit from SysmemBuffer here since it calls get_tt_device() (nullptr) in constructor
class SimulationSysmemBuffer : public SysmemBuffer {
public: 

SimulationSysmemBuffer() = default;

private:

    // TLBManager* tlb_manager_;
    // void* buffer_va_;
    // size_t mapped_buffer_size_;
    // size_t buffer_size_;
};

}