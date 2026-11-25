// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <vector>

#include "simulation/simulation_backend.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

constexpr const char* simulation_not_built_message =
    "Simulation requires a UMD build with -DTT_UMD_BUILD_SIMULATION=ON.";

// Stands in for the simulator in a build without simulation, so callers compile the same in both builds.
class UnavailableSimulationBackend final : public SimulationBackend {
public:
    bool is_server_directory(const std::filesystem::path& /*simulator_path*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    AttachedServer attach_to_server(const std::filesystem::path& /*server_directory*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    tt::ARCH get_simulator_arch(const std::filesystem::path& /*simulator_path*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    SimulationBackendType get_backend_type(const std::filesystem::path& /*simulator_path*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    std::vector<std::string> get_ip_layout_soc_descriptor_paths(
        const std::filesystem::path& /*simulator_path*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> create_ip_layout_tt_devices(
        const std::filesystem::path& /*simulator_path*/,
        const std::map<ChipId, size_t>& /*layout_devices*/,
        int /*num_host_mem_channels*/,
        bool /*launch_simulator*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
        const std::filesystem::path& /*simulator_path*/, int /*num_host_mem_channels*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    std::unique_ptr<TTDevice> create_simulation_tt_device(
        const std::filesystem::path& /*simulator_path*/,
        const SocDescriptor& /*soc_descriptor*/,
        ChipId /*chip_id*/,
        size_t /*num_chips*/,
        int /*num_host_mem_channels*/) const override {
        UMD_THROW(error::RuntimeError, simulation_not_built_message);
    }

    // No simulator exists in this build, so no device is simulated.
    std::optional<uint32_t> get_pci_bdf(const TTDevice& /*tt_device*/) const override { return std::nullopt; }

    void grow_host_mem_channels(TTDevice& /*tt_device*/, int /*num_host_mem_channels*/) const override {}

    std::optional<SimulationBackendType> get_backend_type(const TTDevice& /*tt_device*/) const override {
        return std::nullopt;
    }

    std::optional<DeviceConnection> get_device_connection(const TTDevice& /*tt_device*/) const override {
        return std::nullopt;
    }
};

}  // namespace

const SimulationBackend& simulation_backend() {
    static const UnavailableSimulationBackend backend;
    return backend;
}

}  // namespace tt::umd
