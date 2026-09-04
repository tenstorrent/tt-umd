// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {
class WormholeDeviceFirmware;

class ArchitectureImplementation;
class HangDetector;
class JtagDevice;
class SocArchDescriptor;
class RemoteCommunication;

// Model for a Wormhole device, over any transport that reaches one. Each constructor builds the
// protocol for its transport and, knowing the concrete protocol type, records the interfaces that
// protocol provides -- the rest stay null.
class WormholeTTDeviceModel : public TTDeviceModel {
public:
    WormholeTTDeviceModel(
        std::unique_ptr<PCIDevice> pci_device,
        bool use_safe_api,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    WormholeTTDeviceModel(
        std::unique_ptr<JtagDevice> jtag_device,
        uint8_t jlink_id,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);
    WormholeTTDeviceModel(
        std::unique_ptr<RemoteCommunication> remote_communication,
        const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor);

    ~WormholeTTDeviceModel() override;

    DeviceProtocol *get_device_protocol() override;

    DeviceFirmware *get_device_firmware() override;

    FirmwareTelemetryReader *get_firmware_telemetry_reader() override;

    FirmwareInfoProvider *get_firmware_info_provider() override;

    ArchitectureImplementation *get_architecture_impl() override;

    HangDetector *get_hang_detector() override;

    SocArchDescriptor *get_soc_arch_descriptor() override;

    PcieInterface *get_pcie_interface() override;

    DmaInterface *get_dma_interface() override;

    JtagInterface *get_jtag_interface() override;

    RemoteInterface *get_remote_interface() override;

    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

    PCIDevice *get_pci_device() override;

    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) override;

    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) override;

private:
    std::shared_ptr<SocArchDescriptor> soc_arch_descriptor_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;

    std::unique_ptr<DeviceProtocol> protocol_;
    PcieInterface *pcie_interface_ = nullptr;
    DmaInterface *dma_interface_ = nullptr;
    JtagInterface *jtag_interface_ = nullptr;
    RemoteInterface *remote_interface_ = nullptr;
    // Owned by the PCIe protocol; retained only to serve get_pci_device().
    PCIDevice *pci_device_ = nullptr;

    // Must come after pci_device_: the detector holds a TLB window wired in by TTDevice, which needs
    // the protocol's PCIDevice alive when it is released.
    std::unique_ptr<HangDetector> hang_detector_;

    // Declared after the transports: it borrows them, so it must be destroyed first.
    std::unique_ptr<WormholeDeviceFirmware> device_firmware_;
};

}  // namespace tt::umd
