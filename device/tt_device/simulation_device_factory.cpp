// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/simulation_device_factory.hpp"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/utils/error.hpp"

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
    int num_host_mem_channels,
    std::optional<uint32_t> image_endpoint_count) {
    if (simulator_directory.extension() == ".so") {
        return std::make_unique<TTSimTTDevice>(
            simulator_directory,
            soc_descriptor,
            chip_id,
            num_chips > 1,
            num_host_mem_channels,
            num_chips,
            image_endpoint_count);
    }
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");
    return std::make_unique<RtlSimulationTTDevice>(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels);
}

std::map<ChipId, std::unique_ptr<TTDevice>> create_local_simulation_tt_devices(
    const std::filesystem::path &simulator_path, int num_host_mem_channels) {
    UMD_ASSERT(
        simulator_path.extension() == ".so",
        error::RuntimeError,
        fmt::format(
            "Enumerating a simulator's endpoints needs a libttsim .so; {} is not one. An RTL "
            "simulator exposes no PCI config space to enumerate.",
            simulator_path.string()));

    // Ask the image which endpoints it has, before anything is brought up -- config space only
    // answers while the image runs, and starting an already-running image is fatal.
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path);
    UMD_ASSERT(
        !bdfs.empty(),
        error::RuntimeError,
        fmt::format("Simulator {} exposes no host-visible PCI endpoints.", simulator_path.string()));

    const auto endpoint_count = static_cast<uint32_t>(bdfs.size());
    std::map<ChipId, std::unique_ptr<TTDevice>> devices;
    for (size_t index = 0; index < bdfs.size(); ++index) {
        // Chips are named by their PCI device number, which is the endpoint's position: the
        // communicator folds the chip id into the BDF device field to reach its own endpoint.
        const auto chip_id = static_cast<ChipId>(bdfs[index] >> 3);
        devices.emplace(
            chip_id,
            TTSimTTDevice::create_for_chip(
                simulator_path,
                chip_id,
                num_host_mem_channels,
                /*copy_sim_binary=*/false,
                bdfs.size(),
                endpoint_count));
    }
    log_debug(tt::LogEmulationDriver, "Simulator {} exposes {} endpoint(s)", simulator_path.string(), bdfs.size());
    return devices;
}

}  // namespace tt::umd
