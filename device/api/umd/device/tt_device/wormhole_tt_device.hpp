// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class JtagDevice;
class PCIDevice;
class RemoteCommunication;
enum class IODeviceType;

class WormholeTTDevice : public TTDevice {
public:
    uint32_t get_clock() override;

    uint32_t get_min_clock_freq() override;

    ~WormholeTTDevice() override = default;

protected:
    explicit WormholeTTDevice(std::unique_ptr<TTDeviceModel> model);
<<<<<<< HEAD

private:
    const ArchitectureRegisters registers_ = get_architecture_registers(tt::ARCH::WORMHOLE_B0);

    // Builds the ARC message (with the common prefix) that requests the given clock state.

    friend std::unique_ptr<TTDevice> TTDevice::create(
        int device_number,
        IODeviceType device_type,
        bool use_safe_api,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    friend std::unique_ptr<TTDevice> TTDevice::create(
        std::unique_ptr<RemoteCommunication> remote_communication,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
#ifdef TT_UMD_BUILD_SIMULATION
    friend std::unique_ptr<TTDevice> TTDevice::create_simulation_remote(
        std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor);
#endif  // TT_UMD_BUILD_SIMULATION
=======
>>>>>>> f95cf967 ([Base API] Serve iATU programming through the model)
};
}  // namespace tt::umd
