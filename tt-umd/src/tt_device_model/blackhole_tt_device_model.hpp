// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/tt_device_model/tt_device_model.hpp"
#include "tt_device/firmware/blackhole_device_firmware.hpp"
#include "tt_device/hang_detection/blackhole_hang_detector.hpp"
#include "tt_device/protocol/pcie_protocol.hpp"
#include "tt_device/reset/classic_tile_risc_reset.hpp"

namespace tt::umd {

/**
 * @brief Model of a Blackhole chip on the local PCIe bus.
 */
class BlackholeTTDeviceModel : public TTDeviceModel {
public:
    /**
     * @param pci_device The chip's PCI device, which the model's protocol takes over.
     * @param io_safety Applied to every window the protocol reaches the device through.
     * @param soc_arch_descriptor Descriptor to build the SoC model from, or nullptr for the Blackhole constants.
     */
    BlackholeTTDeviceModel(
        std::unique_ptr<PCIDevice> pci_device,
        IoSafety io_safety,
        const std::shared_ptr<SocArchDescriptor>& soc_arch_descriptor);

    DeviceProtocol* get_device_protocol() override;
    DeviceFirmware* get_device_firmware() override;
    ArchitectureImplementation* get_architecture_impl() override;
    SocArchDescriptor* get_soc_arch_descriptor() override;
    HangDetector* get_hang_detector() override;
    RiscReset* get_risc_reset() override;
    FirmwareTelemetryReader* get_firmware_telemetry_reader() override;
    FirmwareInfoProvider* get_firmware_info_provider() override;
    PcieInterface* get_pcie_interface() override;
    // Served by TTDevice's TLB path.
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override;
    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;
    PCIDevice* get_pci_device() override;

private:
    // Declared in dependency order: each component holds pointers to those above it, and is destroyed first.
    BlackholeImplementation architecture_impl_;
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor_;
    PcieProtocol protocol_;
    BlackholeDeviceFirmware firmware_;
    ClassicTileRiscReset risc_reset_;
    BlackholeHangDetector hang_detector_;
};

}  // namespace tt::umd
