// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"

#include <utility>

#include "tt_device_model/soc_arch_descriptor_resolver.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_protocol.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

WormholeTTDeviceModel::WormholeTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(pci_device->get_device_num()),
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::WORMHOLE_B0>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<WormholeImplementation>()),
    pci_device_(pci_device.get()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_interface_ = pcie_protocol.get();
    dma_interface_ = pcie_protocol.get();
    protocol_ = std::move(pcie_protocol);
    if (use_safe_api) {
        SiliconTlbWindow::set_sigbus_safe_handler(true);
    }

    device_firmware_ = std::make_unique<WormholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, remote_interface_, architecture_impl_.get());
}

WormholeTTDeviceModel::WormholeTTDeviceModel(
    std::unique_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(jlink_id),
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::WORMHOLE_B0>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<WormholeImplementation>()) {
    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_interface_ = jtag_protocol.get();
    protocol_ = std::move(jtag_protocol);

    device_firmware_ = std::make_unique<WormholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, remote_interface_, architecture_impl_.get());
}

// A remote device is reached through a local one, so it takes the local device's identity.
WormholeTTDeviceModel::WormholeTTDeviceModel(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(remote_communication->get_local_device()->get_communication_device_id()),
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::WORMHOLE_B0>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<WormholeImplementation>()) {
    auto remote_protocol = std::make_unique<RemoteProtocol>(std::move(remote_communication));
    remote_interface_ = remote_protocol.get();
    protocol_ = std::move(remote_protocol);

    device_firmware_ = std::make_unique<WormholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, remote_interface_, architecture_impl_.get());
}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
WormholeTTDeviceModel::~WormholeTTDeviceModel() = default;

tt::ARCH WormholeTTDeviceModel::get_arch() const { return tt::ARCH::WORMHOLE_B0; }

int WormholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

DeviceProtocol *WormholeTTDeviceModel::get_device_protocol() { return protocol_.get(); }

DeviceFirmware *WormholeTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

FirmwareTelemetryReader *WormholeTTDeviceModel::get_firmware_telemetry_reader() {
    return device_firmware_->get_firmware_telemetry_reader();
}

FirmwareInfoProvider *WormholeTTDeviceModel::get_firmware_info_provider() {
    return device_firmware_->get_firmware_info_provider();
}

SocArchDescriptor *WormholeTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

ArchitectureImplementation *WormholeTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

std::shared_ptr<SocArchDescriptor> WormholeTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

PcieInterface *WormholeTTDeviceModel::get_pcie_interface() { return pcie_interface_; }

DmaInterface *WormholeTTDeviceModel::get_dma_interface() { return dma_interface_; }

JtagInterface *WormholeTTDeviceModel::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *WormholeTTDeviceModel::get_remote_interface() { return remote_interface_; }

PCIDevice *WormholeTTDeviceModel::get_pci_device() { return pci_device_; }

}  // namespace tt::umd
