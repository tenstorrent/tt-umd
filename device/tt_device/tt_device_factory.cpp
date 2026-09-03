// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_device_factory.hpp"

#include <fmt/format.h>

#include <memory>
#include <utility>

#include "tracy.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"
#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<TTDevice> create_tt_device(
    int device_number,
    IODeviceType device_type,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(
        (!use_safe_api) || (device_type == IODeviceType::PCIe),
        error::RuntimeError,
        "Safe I/O API is not supported for non-PCIe device types.");
    tt::ARCH arch = tt::ARCH::Invalid;
    if (device_type == IODeviceType::JTAG) {
        auto jtag_device = JtagDevice::create();
        arch = jtag_device->get_jtag_arch(device_number);
        switch (arch) {
            case ARCH::WORMHOLE_B0:
                return std::make_unique<WormholeTTDevice>(std::make_unique<WormholeTTDeviceModel>(
                    std::move(jtag_device), device_number, soc_arch_descriptor));
            case ARCH::BLACKHOLE:
                return std::make_unique<BlackholeTTDevice>(std::make_unique<BlackholeTTDeviceModel>(
                    std::move(jtag_device), device_number, soc_arch_descriptor));
            default:
                UMD_THROW(
                    error::RuntimeError,
                    fmt::format("Creating TTDevice is not supported for {} architecture.", arch_to_str(arch)));
        }
    }

    auto pci_device = std::make_unique<PCIDevice>(device_number);
    arch = pci_device->get_arch();

    switch (arch) {
        case ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeTTDevice>(
                std::make_unique<WormholeTTDeviceModel>(std::move(pci_device), use_safe_api, soc_arch_descriptor));
        case ARCH::BLACKHOLE:
            return std::make_unique<BlackholeTTDevice>(
                std::make_unique<BlackholeTTDeviceModel>(std::move(pci_device), use_safe_api, soc_arch_descriptor));
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Creating TTDevice is not supported for {} architecture.", arch_to_str(arch)));
    }
}

std::unique_ptr<TTDevice> create_tt_device(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(remote_communication != nullptr, error::RuntimeError, "RemoteCommunication pointer cannot be null.");
    tt::ARCH arch = remote_communication->get_local_device()->get_arch();
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeTTDevice>(
                std::make_unique<WormholeTTDeviceModel>(std::move(remote_communication), soc_arch_descriptor));
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Remote TTDevice creation is not supported for {} architecture.", arch_to_str(arch)));
    }
}

#ifdef TT_UMD_BUILD_SIMULATION
std::unique_ptr<TTDevice> create_simulation_remote_tt_device(
    std::unique_ptr<RemoteCommunication> remote_communication, const SocDescriptor &soc_descriptor) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(remote_communication != nullptr, error::RuntimeError, "RemoteCommunication pointer cannot be null.");
    tt::ARCH arch = remote_communication->get_local_device()->get_arch();
    UMD_ASSERT(
        soc_descriptor.arch == arch,
        error::RuntimeError,
        fmt::format(
            "Supplied SocDescriptor arch ({}) does not match the remote device arch ({}).",
            arch_to_str(soc_descriptor.arch),
            arch_to_str(arch)));
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0: {
            auto device = std::make_unique<WormholeTTDevice>(std::make_unique<WormholeTTDeviceModel>(
                std::move(remote_communication), /*soc_arch_descriptor=*/nullptr));
            // This device is never run through init_tt_device() (no ARC to probe), so construct_soc_descriptor()
            // never overwrites the descriptor set here; set_soc_descriptor keeps the assign-exactly-once invariant.
            device->set_soc_descriptor(soc_descriptor);
            return device;
        }
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("Remote TTDevice creation is not supported for {} architecture.", arch_to_str(arch)));
    }
}
#endif  // TT_UMD_BUILD_SIMULATION

}  // namespace tt::umd
