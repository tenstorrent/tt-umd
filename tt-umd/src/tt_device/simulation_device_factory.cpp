// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/simulation_device_factory.hpp"

#include <map>
#include <memory>

#include "simulation/simulation_backend.hpp"
#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd {

std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_path, int num_host_mem_channels) {
    return simulation_backend().create_simulation_tt_device(simulator_path, num_host_mem_channels);
}

std::unique_ptr<TTDevice> create_simulation_tt_device(
    const std::filesystem::path &simulator_directory,
    const SocDescriptor &soc_descriptor,
    ChipId chip_id,
    size_t num_chips,
    int num_host_mem_channels) {
    return simulation_backend().create_simulation_tt_device(
        simulator_directory, soc_descriptor, chip_id, num_chips, num_host_mem_channels);
}

std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
    const std::filesystem::path &simulator_path, int num_host_mem_channels) {
    return simulation_backend().create_local_simulation_tt_devices(simulator_path, num_host_mem_channels);
}

}  // namespace tt::umd
