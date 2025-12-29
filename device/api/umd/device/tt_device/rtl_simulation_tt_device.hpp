// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {
class RtlSimulationTTDevice : public TTDevice {
public:
    RtlSimulationTTDevice(const std::filesystem::path &simulator_directory, SocDescriptor soc_descriptor);

    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

private:
    void start_host_communication();

    std::mutex device_lock;
    SimulationHost host;
    std::filesystem::path simulator_directory_;
    SocDescriptor soc_descriptor_;
};
}  // namespace tt::umd
