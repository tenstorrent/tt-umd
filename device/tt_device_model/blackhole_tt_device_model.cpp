// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"

#include <fmt/format.h>

#include <utility>

#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// A caller may supply the descriptor; otherwise it comes from the architecture's constants.
// Named per architecture on purpose: a unity build folds the model sources into one translation
// unit, where a shared name in this anonymous namespace would collide with the other model's copy.
std::shared_ptr<SocArchDescriptor> resolve_blackhole_soc_arch_descriptor(
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) {
    if (soc_arch_descriptor == nullptr) {
        return std::make_shared<SocArchDescriptor>(tt::ARCH::BLACKHOLE);
    }
    UMD_ASSERT(
        soc_arch_descriptor->get_arch() == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format(
            "SocArchDescriptor architecture {} does not match device architecture {}.",
            arch_to_str(soc_arch_descriptor->get_arch()),
            arch_to_str(tt::ARCH::BLACKHOLE)));
    return soc_arch_descriptor;
}

}  // namespace

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(pci_device->get_device_num()),
    soc_arch_descriptor_(resolve_blackhole_soc_arch_descriptor(soc_arch_descriptor)),
    pci_device_(pci_device.get()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_interface_ = pcie_protocol.get();
    dma_interface_ = pcie_protocol.get();
    protocol_ = std::move(pcie_protocol);
    if (use_safe_api) {
        SiliconTlbWindow::set_sigbus_safe_handler(true);
    }
}

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    std::unique_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    communication_device_id_(jlink_id),
    soc_arch_descriptor_(resolve_blackhole_soc_arch_descriptor(soc_arch_descriptor)) {
    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_interface_ = jtag_protocol.get();
    protocol_ = std::move(jtag_protocol);
}

tt::ARCH BlackholeTTDeviceModel::get_arch() const { return tt::ARCH::BLACKHOLE; }

int BlackholeTTDeviceModel::get_communication_device_id() const { return communication_device_id_; }

DeviceProtocol *BlackholeTTDeviceModel::get_device_protocol() { return protocol_.get(); }

SocArchDescriptor *BlackholeTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

std::shared_ptr<SocArchDescriptor> BlackholeTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

PcieInterface *BlackholeTTDeviceModel::get_pcie_interface() { return pcie_interface_; }

DmaInterface *BlackholeTTDeviceModel::get_dma_interface() { return dma_interface_; }

JtagInterface *BlackholeTTDeviceModel::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *BlackholeTTDeviceModel::get_remote_interface() { return remote_interface_; }

PCIDevice *BlackholeTTDeviceModel::get_pci_device() { return pci_device_; }

}  // namespace tt::umd
