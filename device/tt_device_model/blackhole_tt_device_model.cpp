// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"

#include <utility>

#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

namespace tt::umd {

BlackholeTTDeviceModel::BlackholeTTDeviceModel(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api) :
    communication_device_id_(pci_device->get_device_num()), pci_device_(pci_device.get()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_interface_ = pcie_protocol.get();
    dma_interface_ = pcie_protocol.get();
    protocol_ = std::move(pcie_protocol);
    if (use_safe_api) {
        SiliconTlbWindow::set_sigbus_safe_handler(true);
    }
}

BlackholeTTDeviceModel::BlackholeTTDeviceModel(std::unique_ptr<JtagDevice> jtag_device, uint8_t jlink_id) :
    communication_device_id_(jlink_id) {
    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_interface_ = jtag_protocol.get();
    protocol_ = std::move(jtag_protocol);
}

tt::ARCH BlackholeTTDeviceModel::get_arch() const { return tt::ARCH::BLACKHOLE; }

int BlackholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

DeviceProtocol *BlackholeTTDeviceModel::get_device_protocol() { return protocol_.get(); }

PcieInterface *BlackholeTTDeviceModel::get_pcie_interface() { return pcie_interface_; }

DmaInterface *BlackholeTTDeviceModel::get_dma_interface() { return dma_interface_; }

JtagInterface *BlackholeTTDeviceModel::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *BlackholeTTDeviceModel::get_remote_interface() { return remote_interface_; }

PCIDevice *BlackholeTTDeviceModel::get_pci_device() { return pci_device_; }

}  // namespace tt::umd
