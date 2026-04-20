// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/simulation_device_factory.hpp"

#include "tt-umd/tt_device/rtl_simulation_tt_device.hpp"
#include "tt-umd/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels, bool copy_sim_binary) {
    if (simulator_path.extension() == ".so") {
        return TTSimTTDevice::create(simulator_path, num_host_mem_channels, copy_sim_binary);
    }
    return RtlSimulationTTDevice::create(simulator_path, num_host_mem_channels);
}

}  // namespace tt::umd
