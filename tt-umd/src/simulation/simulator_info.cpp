// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/simulation/simulator_info.hpp"

#include "simulation/simulation_backend.hpp"
#include "simulation/simulator_paths.hpp"
#include "tt-umd/tt_device/remote_communication.hpp"
#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd {

tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path) {
    return simulation_backend().get_simulator_arch(simulator_path);
}

std::string find_simulator_soc_descriptor_path(const std::filesystem::path& simulator_path) {
    std::string soc_descriptor_path = get_simulator_soc_descriptor_path(simulator_path);
    return std::filesystem::exists(soc_descriptor_path) ? soc_descriptor_path : "";
}

std::optional<SimulationBackendType> get_simulation_backend(const TTDevice& device) {
    // A remote device is backed by whatever backs the device it is reached through, e.g. the second chip of a
    // multichip TTSim image is reached over Ethernet from the simulated chip the image exposes to the host.
    if (device.is_remote()) {
        // get_remote_communication has no const overload; it is only read here.
        auto& mutable_device = const_cast<TTDevice&>(device);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
        return get_simulation_backend(*mutable_device.get_remote_communication()->get_local_device());
    }
    return simulation_backend().get_backend_type(device);
}

}  // namespace tt::umd
