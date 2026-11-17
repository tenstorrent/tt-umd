// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device_model/blackhole_tt_device_model.hpp"

#include <memory>
#include <utility>

#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"

namespace tt::umd {

namespace {

// The hang detector's own window: it probes while an I/O through the protocol holds the protocol's windows. It
// shares their io_safety because the probe runs inside a stalled guarded access: a fault during the probe must
// surface from the probe itself, unwinding the detector's lock, rather than jump past it to the outer access.
std::unique_ptr<TlbWindow> make_probe_window(PCIDevice& pci_device, IoSafety io_safety) {
    const size_t size = get_architecture_tlbs(pci_device.get_arch()).cached_window_size;
    return std::make_unique<SiliconTlbWindow>(pci_device.allocate_tlb(size, TlbMapping::UC), tlb_data{}, io_safety);
}

}  // namespace

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device,
    IoSafety io_safety,
    const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor) :
    soc_arch_descriptor_(
        soc_arch_descriptor != nullptr ? soc_arch_descriptor
                                       : std::make_shared<SocArchDescriptor>(tt::ARCH::BLACKHOLE)),
    protocol_(std::move(pci_device), io_safety),
    firmware_(&protocol_, &protocol_, &architecture_impl_),
    risc_reset_(&protocol_, &architecture_impl_),
    hang_detector_(
        &protocol_,
        make_probe_window(*protocol_.get_pci_device(), io_safety),
        firmware_.get_firmware_noc_coord(NocId::NOC0),
        firmware_.get_firmware_noc_coord(NocId::NOC1)),
    dma_(protocol_.get_pci_device()) {}

DeviceProtocol* BlackholeTTDeviceModel::get_device_protocol() { return &protocol_; }

DeviceFirmware* BlackholeTTDeviceModel::get_device_firmware() { return &firmware_; }

ArchitectureImplementation* BlackholeTTDeviceModel::get_architecture_impl() { return &architecture_impl_; }

SocArchDescriptor* BlackholeTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

HangDetector* BlackholeTTDeviceModel::get_hang_detector() { return &hang_detector_; }

RiscReset* BlackholeTTDeviceModel::get_risc_reset() { return &risc_reset_; }

FirmwareTelemetryReader* BlackholeTTDeviceModel::get_firmware_telemetry_reader() {
    return firmware_.get_firmware_telemetry_reader();
}

FirmwareInfoProvider* BlackholeTTDeviceModel::get_firmware_info_provider() {
    return firmware_.get_firmware_info_provider();
}

PcieInterface* BlackholeTTDeviceModel::get_pcie_interface() { return &protocol_; }

DmaInterface* BlackholeTTDeviceModel::get_dma_interface() { return &dma_; }

std::unique_ptr<IoWindow> BlackholeTTDeviceModel::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host) {
    return nullptr;
}

std::shared_ptr<SocArchDescriptor> BlackholeTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

PCIDevice* BlackholeTTDeviceModel::get_pci_device() { return protocol_.get_pci_device(); }

}  // namespace tt::umd
