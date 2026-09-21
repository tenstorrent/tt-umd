// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/quasar_tt_device_model.hpp"

#include "soc_arch_descriptor_resolver.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/tt_device/firmware/quasar_device_firmware.hpp"
#include "umd/device/tt_device/protocol/quasar_protocol.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

QuasarTTDeviceModel::QuasarTTDeviceModel(
    std::unique_ptr<ScalarNocAccess> access,
    int mmio_id,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::QUASAR>(soc_arch_descriptor)),
    architecture_impl_(ArchitectureImplementation::create(tt::ARCH::QUASAR)),
    protocol_(std::make_unique<QuasarProtocol>(std::move(access), mmio_id)),
    device_firmware_(std::make_unique<QuasarDeviceFirmware>()) {}

QuasarTTDeviceModel::~QuasarTTDeviceModel() = default;

DeviceProtocol *QuasarTTDeviceModel::get_device_protocol() { return protocol_.get(); }

DeviceFirmware *QuasarTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

ArchitectureImplementation *QuasarTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

SocArchDescriptor *QuasarTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

std::shared_ptr<SocArchDescriptor> QuasarTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

}  // namespace tt::umd
