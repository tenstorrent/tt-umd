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
    std::chrono::milliseconds wait_eth_core_training(
        CoreCoord eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT);
    uint32_t get_risc_reset_state(CoreCoord core);
    void set_risc_reset_state(CoreCoord core, const uint32_t risc_flags);
    void dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr);
    void dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr);
    void dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr);
    EthTrainingStatus read_eth_core_training_status(CoreCoord eth_core);

private:
    TTDevice* tt_device_ = nullptr;
    SocDescriptor soc_descriptor_;
};

}  // namespace tt::umd
