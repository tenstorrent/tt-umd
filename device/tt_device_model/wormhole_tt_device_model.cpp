// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/wormhole_tt_device_model.hpp"

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "noc_access.hpp"
#include "tt_device_model/soc_arch_descriptor_resolver.hpp"
#include "umd/device/arch/architecture_registers.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/wormhole_hang_detector.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_protocol.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

WormholeTTDeviceModel::WormholeTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::WORMHOLE_B0>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<WormholeImplementation>()),
    pci_device_(pci_device.get()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_interface_ = pcie_protocol.get();
    dma_interface_ = pcie_protocol.get();
    protocol_ = std::move(pcie_protocol);
    hang_detector_ = std::make_unique<WormholeHangDetector>(protocol_.get());
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
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::WORMHOLE_B0>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<WormholeImplementation>()) {
    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_interface_ = jtag_protocol.get();
    protocol_ = std::move(jtag_protocol);
    hang_detector_ = std::make_unique<WormholeHangDetector>(protocol_.get());

    device_firmware_ = std::make_unique<WormholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, remote_interface_, architecture_impl_.get());
}

WormholeTTDeviceModel::WormholeTTDeviceModel(
    std::unique_ptr<RemoteCommunication> remote_communication,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::WORMHOLE_B0>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<WormholeImplementation>()) {
    auto remote_protocol = std::make_unique<RemoteProtocol>(std::move(remote_communication));
    remote_interface_ = remote_protocol.get();
    protocol_ = std::move(remote_protocol);
    hang_detector_ = std::make_unique<WormholeHangDetector>(
        remote_interface_->get_remote_communication()->get_local_device()->get_device_protocol());

    device_firmware_ = std::make_unique<WormholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, remote_interface_, architecture_impl_.get());
}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
WormholeTTDeviceModel::~WormholeTTDeviceModel() = default;

DeviceProtocol *WormholeTTDeviceModel::get_device_protocol() { return protocol_.get(); }

DeviceFirmware *WormholeTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

void WormholeTTDeviceModel::read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    device_firmware_->read_from_arc_apb(mem_ptr, arc_addr_offset, size, noc_id);
}

void WormholeTTDeviceModel::write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    device_firmware_->write_to_arc_apb(mem_ptr, arc_addr_offset, size, noc_id);
}

void WormholeTTDeviceModel::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    uint32_t dest_bar_lo = target & 0xffffffff;
    uint32_t dest_bar_hi = (target >> 32) & 0xffffffff;
    std::uint32_t region_id_to_use = region;

    // TODO: stop doing this.  It's related to HUGEPAGE_CHANNEL_3_SIZE_LIMIT.
    if (region == 3) {
        region_id_to_use = 4;  // Hack use region 4 for channel 3..this ensures that we have a smaller chan 3 address
                               // space with the correct start offset
    }

    if (jtag_interface_ != nullptr) {
        UMD_THROW(error::RuntimeError, "configure_iatu_region is redundant for JTAG communication type.");
    }

    const ArchitectureRegisters &registers = get_architecture_registers(tt::ARCH::WORMHOLE_B0);
    pcie_interface_->bar_write32(registers.arc_csm_bar0_mailbox_offset + 0 * 4, region_id_to_use);
    pcie_interface_->bar_write32(registers.arc_csm_bar0_mailbox_offset + 1 * 4, dest_bar_lo);
    pcie_interface_->bar_write32(registers.arc_csm_bar0_mailbox_offset + 2 * 4, dest_bar_hi);
    pcie_interface_->bar_write32(registers.arc_csm_bar0_mailbox_offset + 3 * 4, region_size);
    device_firmware_->send_device_command(
        wormhole::ARC_MSG_COMMON_PREFIX |
            static_cast<uint32_t>(wormhole::arc_message_type::SETUP_IATU_FOR_PEER_TO_PEER),
        {0, 0},
        timeout::ARC_MESSAGE_TIMEOUT,
        get_selected_noc_id());

    // Print what just happened.
    uint32_t peer_region_start = region_id_to_use * region_size;
    uint32_t peer_region_end = (region_id_to_use + 1) * region_size - 1;
    log_debug(
        LogUMD,
        "    [region id {}] NOC to PCI address range 0x{:x}-0x{:x} mapped to addr 0x{:x}",
        region,
        peer_region_start,
        peer_region_end,
        target);
}

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

HangDetector *WormholeTTDeviceModel::get_hang_detector() { return hang_detector_.get(); }

PcieInterface *WormholeTTDeviceModel::get_pcie_interface() { return pcie_interface_; }

DmaInterface *WormholeTTDeviceModel::get_dma_interface() { return dma_interface_; }

JtagInterface *WormholeTTDeviceModel::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *WormholeTTDeviceModel::get_remote_interface() { return remote_interface_; }

PCIDevice *WormholeTTDeviceModel::get_pci_device() { return pci_device_; }

}  // namespace tt::umd
