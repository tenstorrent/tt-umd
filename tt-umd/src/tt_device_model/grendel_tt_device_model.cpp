// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device_model/grendel_tt_device_model.hpp"

#include "io_window/kmd_noc_window.hpp"
#include "soc_arch_descriptor_resolver.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt_device/firmware/grendel_device_firmware.hpp"
#include "tt_device/protocol/kmd_protocol.hpp"

namespace tt::umd {

GrendelTTDeviceModel::GrendelTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device, int mmio_id, const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::QUASAR>(soc_arch_descriptor)),
    architecture_impl_(ArchitectureImplementation::create(tt::ARCH::QUASAR)),
    pci_device_(pci_device.get()),
    protocol_(std::make_unique<KmdProtocol>(std::move(pci_device), mmio_id)),
    device_firmware_(std::make_unique<GrendelDeviceFirmware>()) {}

GrendelTTDeviceModel::~GrendelTTDeviceModel() = default;

// The window borrows the device the protocol owns: both issue their accesses over the same driver
// handle, and this model outlives the windows it hands out.
std::unique_ptr<IoWindow> GrendelTTDeviceModel::create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) {
    return std::make_unique<KmdNocWindow>(pci_device_, target, host.size);
}

DeviceProtocol *GrendelTTDeviceModel::get_device_protocol() { return protocol_.get(); }

PCIDevice *GrendelTTDeviceModel::get_pci_device() { return pci_device_; }

DeviceFirmware *GrendelTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

ArchitectureImplementation *GrendelTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

SocArchDescriptor *GrendelTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

att::EndpointResolver *GrendelTTDeviceModel::get_endpoint_resolver() { return &endpoint_resolver_; }

std::shared_ptr<SocArchDescriptor> GrendelTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

}  // namespace tt::umd
