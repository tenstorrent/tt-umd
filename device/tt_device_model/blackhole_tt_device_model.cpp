// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"

#include <utility>

#include "tt_device_model/soc_arch_descriptor_resolver.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

namespace tt::umd {

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(pci_device->get_device_num()),
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::BLACKHOLE>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<BlackholeImplementation>()),
    pci_device_(pci_device.get()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_interface_ = pcie_protocol.get();
    dma_interface_ = pcie_protocol.get();
    protocol_ = std::move(pcie_protocol);
    if (use_safe_api) {
        SiliconTlbWindow::set_sigbus_safe_handler(true);
    }

    device_firmware_ = std::make_unique<BlackholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, architecture_impl_.get());
}

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    std::unique_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(jlink_id),
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::BLACKHOLE>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<BlackholeImplementation>()) {
    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_interface_ = jtag_protocol.get();
    protocol_ = std::move(jtag_protocol);

    device_firmware_ = std::make_unique<BlackholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, architecture_impl_.get());
}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
BlackholeTTDeviceModel::~BlackholeTTDeviceModel() = default;

tt::ARCH BlackholeTTDeviceModel::get_arch() const { return tt::ARCH::BLACKHOLE; }

int BlackholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

DeviceProtocol *BlackholeTTDeviceModel::get_device_protocol() { return protocol_.get(); }

DeviceFirmware *BlackholeTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

SocArchDescriptor *BlackholeTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

ArchitectureImplementation *BlackholeTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

std::shared_ptr<SocArchDescriptor> BlackholeTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

PcieInterface *BlackholeTTDeviceModel::get_pcie_interface() { return pcie_interface_; }

DmaInterface *BlackholeTTDeviceModel::get_dma_interface() { return dma_interface_; }

JtagInterface *BlackholeTTDeviceModel::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *BlackholeTTDeviceModel::get_remote_interface() { return remote_interface_; }

PCIDevice *BlackholeTTDeviceModel::get_pci_device() { return pci_device_; }

}  // namespace tt::umd
