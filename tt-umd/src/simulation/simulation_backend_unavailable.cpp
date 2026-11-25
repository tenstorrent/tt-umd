// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/simulation_backend.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr const char* simulation_not_built_message =
    "Simulation requires a UMD build with -DTT_UMD_BUILD_SIMULATION=ON.";

// Stands in for the simulator in a build without simulation, so callers compile the same in both builds.
class UnavailableSimulationBackend final : public SimulationBackend {
public:
    tt::ARCH get_simulator_arch(const std::filesystem::path& /*simulator_path*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
        const std::filesystem::path& /*simulator_path*/, int /*num_host_mem_channels*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    // No simulator exists in this build, so no device is simulated.
    std::optional<uint32_t> get_pci_bdf(const TTDevice& /*tt_device*/) const override { return std::nullopt; }

    std::optional<SimulationBackendType> get_backend_type(const TTDevice& /*tt_device*/) const override {
        return std::nullopt;
    }
};

}  // namespace

const SimulationBackend& simulation_backend() {
    static const UnavailableSimulationBackend backend;
    return backend;
}

}  // namespace tt::umd
