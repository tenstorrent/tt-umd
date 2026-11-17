// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device_model/blackhole_tt_device_model.hpp"

#include <utility>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device/firmware/blackhole_device_firmware.hpp"
#include "tt_device/hang_detection/blackhole_hang_detector.hpp"
#include "tt_device/protocol/pcie_dma/blackhole_dma_transfer.hpp"
#include "tt_device/protocol/pcie_protocol.hpp"
#include "tt_device/reset/classic_tile_risc_reset.hpp"

namespace tt::umd {

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    int pci_device_number, IoSafety io_safety, std::shared_ptr<SocArchDescriptor> soc_arch_descriptor) :
    architecture_impl_(std::make_unique<BlackholeImplementation>()),
    soc_arch_descriptor_(
        soc_arch_descriptor ? std::move(soc_arch_descriptor)
                            : std::make_shared<SocArchDescriptor>(tt::ARCH::BLACKHOLE)),
    pcie_protocol_(std::make_unique<PcieProtocol>(
        std::make_unique<PCIDevice>(pci_device_number), architecture_impl_.get(), io_safety)),
    device_firmware_(std::make_unique<BlackholeDeviceFirmware>(
        pcie_protocol_.get(), pcie_protocol_.get(), nullptr, architecture_impl_.get())),
    hang_detector_(std::make_unique<BlackholeHangDetector>(pcie_protocol_.get(), pcie_protocol_.get())),
    risc_reset_(std::make_unique<ClassicTileRiscReset>(pcie_protocol_.get(), architecture_impl_.get())),
    dma_transfer_(std::make_unique<BlackholeDmaTransfer>(pcie_protocol_->get_pci_device())) {}

BlackholeTTDeviceModel::~BlackholeTTDeviceModel() = default;

DeviceProtocol* BlackholeTTDeviceModel::get_device_protocol() { return pcie_protocol_.get(); }

DeviceFirmware* BlackholeTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

ArchitectureImplementation* BlackholeTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

SocArchDescriptor* BlackholeTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

HangDetector* BlackholeTTDeviceModel::get_hang_detector() { return hang_detector_.get(); }

RiscReset* BlackholeTTDeviceModel::get_risc_reset() { return risc_reset_.get(); }

FirmwareTelemetryReader* BlackholeTTDeviceModel::get_firmware_telemetry_reader() {
    return device_firmware_->get_firmware_telemetry_reader();
}

FirmwareInfoProvider* BlackholeTTDeviceModel::get_firmware_info_provider() {
    return device_firmware_->get_firmware_info_provider();
}

PcieInterface* BlackholeTTDeviceModel::get_pcie_interface() { return pcie_protocol_.get(); }

DmaInterface* BlackholeTTDeviceModel::get_dma_interface() { return dma_transfer_.get(); }

std::unique_ptr<IoWindow> BlackholeTTDeviceModel::create_io_window(
    TargetIoWindowConfig target, HostIoWindowConfig host) {
    UMD_THROW(error::RuntimeError, "BlackholeTTDeviceModel::create_io_window is not implemented yet");
}

std::shared_ptr<SocArchDescriptor> BlackholeTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

PCIDevice* BlackholeTTDeviceModel::get_pci_device() { return pcie_protocol_->get_pci_device(); }

}  // namespace tt::umd
