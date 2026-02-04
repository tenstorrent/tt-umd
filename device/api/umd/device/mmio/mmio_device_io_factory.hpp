// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <optional>

#include "umd/device/mmio/mmio_device_io.hpp"
#include "umd/device/mmio/rtl_simulation_mmio_device_io.hpp"
#include "umd/device/mmio/silicon_mmio_device_io.hpp"
#include "umd/device/mmio/ttsim_mmio_device_io.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_kmd_lib/tt_kmd_lib.h"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

enum class SimulationType { TTSim, RTLSimulation };

/**
 * Factory class for creating appropriate MMIODeviceIO implementations.
 */
class MMIODeviceIOFactory {
public:

    /**
     * Create a TTSim-based MMIO device IO implementation.
     *
     * @param simulator_directory Path to the TTSim simulator directory.
     * @param soc_descriptor SoC descriptor for the device.
     * @param size Size of the memory window.
     * @param base_address Base address for the window.
     * @param config Initial TLB configuration.
     * @return Unique pointer to TTSimMMIODeviceIO instance.
     */
    static std::unique_ptr<MMIODeviceIO> create_ttsim_mmio(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        size_t size,
        uint64_t base_address = 0,
        const tlb_data& config = {});

    /**
     * Create an RTL simulation-based MMIO device IO implementation.
     *
     * @param simulator_directory Path to the RTL simulator directory.
     * @param soc_descriptor SoC descriptor for the device.
     * @param size Size of the memory window.
     * @param base_address Base address for the window.
     * @param config Initial TLB configuration.
     * @return Unique pointer to RTLSimulationMMIODeviceIO instance.
     */
    static std::unique_ptr<MMIODeviceIO> create_rtl_simulation_mmio(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        size_t size,
        uint64_t base_address = 0,
        const tlb_data& config = {});

    /**
     * Create appropriate MMIO device IO implementation based on device type.
     *
     * @param simulation_type Type of simulation (TTSim or RTLSimulation), or none for silicon.
     * @param tt_device Pointer to tt_device (required for silicon, can be nullptr for simulation).
     * @param simulator_directory Path to simulator directory (required for simulation).
     * @param soc_descriptor SoC descriptor (required for simulation).
     * @param size Size of the memory window.
     * @param tlb_mapping TLB mapping type (only used for silicon).
     * @param base_address Base address (used for simulation).
     * @param config Initial TLB configuration.
     * @return Unique pointer to appropriate MMIODeviceIO implementation.
     */
    static std::unique_ptr<MMIODeviceIO> create_mmio(
        const std::optional<SimulationType>& simulation_type,
        tt_device_t* tt_device,
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        size_t size,
        const TlbMapping tlb_mapping = TlbMapping::UC,
        uint64_t base_address = 0,
        const tlb_data& config = {});

    /**
     * Create silicon MMIO device IO implementation (convenience method).
     */
    static std::unique_ptr<MMIODeviceIO> create_silicon_mmio(
        tt_device_t* tt_device, size_t size, const TlbMapping tlb_mapping = TlbMapping::UC, const tlb_data& config = {});
};

}  // namespace tt::umd