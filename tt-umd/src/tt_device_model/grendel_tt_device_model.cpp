// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device_model/grendel_tt_device_model.hpp"

#include "soc_arch_descriptor_resolver.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/io_window/kmd_noc_window.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/tt_device/firmware/grendel_device_firmware.hpp"
#include "tt-umd/tt_device/protocol/grendel_protocol.hpp"
#include "tt-umd/types/arch.hpp"

namespace tt::umd {

GrendelTTDeviceModel::GrendelTTDeviceModel(
    std::shared_ptr<KmdScalarNocAccess> access,
    int mmio_id,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    access_(std::move(access)),
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::QUASAR>(soc_arch_descriptor)),
    architecture_impl_(ArchitectureImplementation::create(tt::ARCH::QUASAR)),
    protocol_(std::make_unique<GrendelProtocol>(access_, mmio_id)),
    device_firmware_(std::make_unique<GrendelDeviceFirmware>()) {}

GrendelTTDeviceModel::~GrendelTTDeviceModel() = default;

// The window and the protocol issue their accesses over the same driver handle, which is why the
// access is shared rather than owned by whichever of them was built first.
std::unique_ptr<IoWindow> GrendelTTDeviceModel::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return std::make_unique<KmdNocWindow>(access_, target, host.size);
}

DeviceProtocol *GrendelTTDeviceModel::get_device_protocol() { return protocol_.get(); }

DeviceFirmware *GrendelTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

ArchitectureImplementation *GrendelTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

SocArchDescriptor *GrendelTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

att::EndpointResolver *GrendelTTDeviceModel::get_endpoint_resolver() { return &endpoint_resolver_; }

std::shared_ptr<SocArchDescriptor> GrendelTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

}  // namespace tt::umd
