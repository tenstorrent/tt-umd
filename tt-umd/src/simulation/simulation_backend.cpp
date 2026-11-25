// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_backend.hpp"

#include "simulation/simulator_paths.hpp"
#include "tt-umd/tt_device/simulation_device_factory.hpp"
#include "tt-umd/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

namespace {

class AvailableSimulationBackend final : public SimulationBackend {
public:
    tt::ARCH get_simulator_arch(const std::filesystem::path& simulator_path) const override {
        return tt::umd::get_simulator_arch(simulator_path);
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
        const std::filesystem::path& simulator_path, int num_host_mem_channels) const override {
        return tt::umd::create_local_simulation_tt_devices(simulator_path, num_host_mem_channels);
    }

    // Only a TTSim device is opened on an endpoint its image enumerated; RTL devices and silicon have no such BDF.
    std::optional<uint32_t> get_pci_bdf(const TTDevice& tt_device) const override {
        if (const auto* tt_sim_device = dynamic_cast<const TTSimTTDevice*>(&tt_device)) {
            return tt_sim_device->get_pci_bdf();
        }
        return std::nullopt;
    }
};

}  // namespace

const SimulationBackend& simulation_backend() {
    static const AvailableSimulationBackend backend;
    return backend;
}

}  // namespace tt::umd
