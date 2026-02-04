// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/mmio/mmio_device_io_factory.hpp"

#include <stdexcept>

namespace tt::umd {

std::unique_ptr<MMIODeviceIO> MMIODeviceIOFactory::create_silicon_mmio(
    tt_device_t* tt_device, size_t size, const TlbMapping tlb_mapping, const tlb_data& config) {
    if (tt_device == nullptr) {
        throw std::invalid_argument("tt_device cannot be nullptr for silicon MMIO");
    }
    return std::make_unique<SiliconMMIODeviceIO>(tt_device, size, tlb_mapping, config);
}

#ifdef TT_UMD_BUILD_SIMULATION
std::unique_ptr<MMIODeviceIO> MMIODeviceIOFactory::create_ttsim_mmio(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    size_t size,
    uint64_t base_address,
    const tlb_data& config) {
    return std::make_unique<TTSimMMIODeviceIO>(simulator_directory, soc_descriptor, size, base_address, config);
}

std::unique_ptr<MMIODeviceIO> MMIODeviceIOFactory::create_rtl_simulation_mmio(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    size_t size,
    uint64_t base_address,
    const tlb_data& config) {
    return std::make_unique<RTLSimulationMMIODeviceIO>(simulator_directory, soc_descriptor, size, base_address, config);
}
#else
std::unique_ptr<MMIODeviceIO> MMIODeviceIOFactory::create_ttsim_mmio(
    const std::filesystem::path& /*simulator_directory*/,
    const SocDescriptor& /*soc_descriptor*/,
    size_t /*size*/,
    uint64_t /*base_address*/,
    const tlb_data& /*config*/) {
    throw std::runtime_error("TTSim MMIO not available - UMD built without simulation support");
}

std::unique_ptr<MMIODeviceIO> MMIODeviceIOFactory::create_rtl_simulation_mmio(
    const std::filesystem::path& /*simulator_directory*/,
    const SocDescriptor& /*soc_descriptor*/,
    size_t /*size*/,
    uint64_t /*base_address*/,
    const tlb_data& /*config*/) {
    throw std::runtime_error("RTL simulation MMIO not available - UMD built without simulation support");
}
#endif

std::unique_ptr<MMIODeviceIO> MMIODeviceIOFactory::create_mmio(
    const std::optional<SimulationType>& simulation_type,
    tt_device_t* tt_device,
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    size_t size,
    const TlbMapping tlb_mapping,
    uint64_t base_address,
    const tlb_data& config) {
    if (simulation_type.has_value()) {
#ifdef TT_UMD_BUILD_SIMULATION
        switch (simulation_type.value()) {
            case SimulationType::TTSim:
                return create_ttsim_mmio(simulator_directory, soc_descriptor, size, base_address, config);
            case SimulationType::RTLSimulation:
                return create_rtl_simulation_mmio(simulator_directory, soc_descriptor, size, base_address, config);
            default:
                throw std::invalid_argument("Unknown simulation type");
        }
#else
        throw std::runtime_error("Simulation MMIO not available - UMD built without simulation support");
#endif
    } else {
        return create_silicon_mmio(tt_device, size, tlb_mapping, config);
    }
}

}  // namespace tt::umd