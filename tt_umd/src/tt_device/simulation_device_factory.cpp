// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_umd/tt_device/simulation_device_factory.hpp"

#include "tt_umd/tt_device/rtl_simulation_tt_device.hpp"
#include "tt_umd/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

std::filesystem::path get_simulation_soc_descriptor_path(const std::filesystem::path &simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "soc_descriptor.yaml")
                                                 : (simulator_path / "soc_descriptor.yaml");
}

std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels, bool copy_sim_binary) {
    if (simulator_path.extension() == ".so") {
        return TTSimTTDevice::create(simulator_path, num_host_mem_channels, copy_sim_binary);
    }
    return RtlSimulationTTDevice::create(simulator_path, num_host_mem_channels);
}

}  // namespace tt::umd
