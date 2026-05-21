// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/simulation_device_factory.hpp"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels, bool copy_sim_binary) {
    if (simulator_path.extension() == ".so") {
        return TTSimTTDevice::create(simulator_path, num_host_mem_channels, copy_sim_binary);
    }
    return RtlSimulationTTDevice::create(simulator_path, num_host_mem_channels);
}

std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_directory,
    const SocDescriptor &soc_descriptor,
    ChipId chip_id,
    size_t num_chips,
    int num_host_mem_channels) {
    if (simulator_directory.extension() == ".so") {
        return std::make_unique<TTSimTTDevice>(
            simulator_directory, soc_descriptor, chip_id, num_chips > 1, num_host_mem_channels);
    }
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");
    return std::make_unique<RtlSimulationTTDevice>(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels);
}

}  // namespace tt::umd
