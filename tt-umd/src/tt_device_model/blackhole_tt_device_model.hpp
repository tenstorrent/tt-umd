// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/soc_arch_descriptor.hpp"
#include "tt-umd/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

class BlackholeDeviceFirmware;
class BlackholeDmaTransfer;
class BlackholeHangDetector;
class ClassicTileRiscReset;
class PcieProtocol;

/**
 * @brief Components of a Blackhole chip reached over PCIe.
 */
class BlackholeTTDeviceModel : public TTDeviceModel {
public:
    /**
     * @param pci_device_number N in /dev/tenstorrent/N.
     * @param io_safety Whether window accesses recover from SIGBUS instead of crashing the process.
     * @param soc_arch_descriptor Descriptor to share; nullptr builds Blackhole's default one.
     */
    BlackholeTTDeviceModel(
        int pci_device_number, IoSafety io_safety, std::shared_ptr<SocArchDescriptor> soc_arch_descriptor = nullptr);

    ~BlackholeTTDeviceModel() override;

    DeviceProtocol* get_device_protocol() override;
    DeviceFirmware* get_device_firmware() override;
    ArchitectureImplementation* get_architecture_impl() override;
    SocArchDescriptor* get_soc_arch_descriptor() override;
    HangDetector* get_hang_detector() override;
    RiscReset* get_risc_reset() override;
    FirmwareTelemetryReader* get_firmware_telemetry_reader() override;
    FirmwareInfoProvider* get_firmware_info_provider() override;
    PcieInterface* get_pcie_interface() override;
    DmaInterface* get_dma_interface() override;
    std::unique_ptr<IoWindow> create_io_window(TargetIoWindowConfig target, HostIoWindowConfig host) override;
    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;
    PCIDevice* get_pci_device() override;

private:
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor_;
    std::unique_ptr<PcieProtocol> pcie_protocol_;
    std::unique_ptr<BlackholeDeviceFirmware> device_firmware_;
    std::unique_ptr<BlackholeHangDetector> hang_detector_;
    std::unique_ptr<ClassicTileRiscReset> risc_reset_;
    std::unique_ptr<BlackholeDmaTransfer> dma_transfer_;
};

}  // namespace tt::umd
