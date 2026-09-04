// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/blackhole_tt_device_model.hpp"

#include <sys/mman.h>  // for MAP_FAILED

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt_device_model/soc_arch_descriptor_resolver.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"
#include "umd/device/tt_device/hang_detection/blackhole_hang_detector.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {

// Whether NOC address translation is on, read from the NOC0 NIU config. The Blackhole hang detector
// needs this at construction to pick the core it probes.
// TODO: temporary - BlackholeTTDevice reads the same register for its own callers; both collapse into
// DeviceFirmware::get_noc_translation_enabled() once that component exists.
bool read_noc_translation_enabled(PcieInterface *pcie_interface, JtagInterface *jtag_interface) {
    const uint32_t niu_cfg = jtag_interface != nullptr
                                 ? jtag_interface->mmio_read32(blackhole::NIU_CFG_NOC0_ARC_ADDR)
                                 : pcie_interface->bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + 0x100);
    return ((niu_cfg >> 14) & 0x1) != 0;
}

}  // namespace

BlackholeTTDeviceModel::BlackholeTTDeviceModel(
    std::unique_ptr<PCIDevice> pci_device,
    bool use_safe_api,
    const std::shared_ptr<SocArchDescriptor> &soc_arch_descriptor) :
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::BLACKHOLE>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<BlackholeImplementation>()),
    pci_device_(pci_device.get()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(std::move(pci_device), use_safe_api);
    pcie_interface_ = pcie_protocol.get();
    dma_interface_ = pcie_protocol.get();
    protocol_ = std::move(pcie_protocol);
    hang_detector_ = std::make_unique<BlackholeHangDetector>(
        protocol_.get(), read_noc_translation_enabled(pcie_interface_, /*jtag_interface=*/nullptr));
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
    soc_arch_descriptor_(resolve_soc_arch_descriptor<tt::ARCH::BLACKHOLE>(soc_arch_descriptor)),
    architecture_impl_(std::make_unique<BlackholeImplementation>()) {
    auto jtag_protocol = std::make_unique<JtagProtocol>(std::move(jtag_device), jlink_id);
    jtag_interface_ = jtag_protocol.get();
    protocol_ = std::move(jtag_protocol);
    hang_detector_ = std::make_unique<BlackholeHangDetector>(
        protocol_.get(), read_noc_translation_enabled(/*pcie_interface=*/nullptr, jtag_interface_));

    device_firmware_ = std::make_unique<BlackholeDeviceFirmware>(
        protocol_.get(), pcie_interface_, jtag_interface_, architecture_impl_.get());
}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
BlackholeTTDeviceModel::~BlackholeTTDeviceModel() {
    // Turn off iATU for the regions we programmed.  This won't happen if the
    // application crashes -- this is a good example of why userspace should not
    // be touching this hardware resource directly -- but it's a good idea to
    // clean up after ourselves.
    if (pci_device_ == nullptr) {
        return;
    }
    if (pci_device_->bar2_uc != nullptr && pci_device_->bar2_uc != MAP_FAILED) {
        auto *bar2 = static_cast<volatile uint8_t *>(pci_device_->bar2_uc);

        for (size_t region : iatu_regions_) {
            uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);
            uint64_t region_ctrl_2 = 0;
            *reinterpret_cast<volatile uint32_t *>(bar2 + iatu_base + 0x04) = region_ctrl_2;
        }
    }
}

DeviceProtocol *BlackholeTTDeviceModel::get_device_protocol() { return protocol_.get(); }

DeviceFirmware *BlackholeTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

void BlackholeTTDeviceModel::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    uint64_t base = region * region_size;
    uint64_t iatu_base = ATU_OFFSET_IN_BH_BAR2 + (region * 0x200);
    // A JTAG-attached device has no PCIDevice, so it reports the same failure an unmapped BAR2 did.
    auto *bar2 = pci_device_ != nullptr ? static_cast<volatile uint8_t *>(pci_device_->bar2_uc) : nullptr;

    if (region_size % (1ULL << 30) != 0 || region_size > (1ULL << 32)) {
        // If you hit this, the suggestion is to not use iATU: map your buffer
        // with the driver, and use the IOVA it provides in your device code.
        UMD_THROW(
            error::RuntimeError, "Failed constraint: region_size % (1ULL << 30) == 0; region_size <= (1ULL <<32).");
    }

    if (bar2 == nullptr || bar2 == MAP_FAILED) {
        UMD_THROW(error::RuntimeError, "BAR2 not mapped.");
    }

    auto write_iatu_reg = [bar2](uint64_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(bar2 + offset) = value;
    };

    uint64_t limit = (base + (region_size - 1)) & 0xffff'ffff;
    uint32_t base_lo = (base >> 0x00) & 0xffff'ffff;
    uint32_t base_hi = (base >> 0x20) & 0xffff'ffff;
    uint32_t target_lo = (target >> 0x00) & 0xffff'ffff;
    uint32_t target_hi = (target >> 0x20) & 0xffff'ffff;

    uint32_t region_ctrl_1 = 0;
    uint32_t region_ctrl_2 = 1 << 31;  // REGION_EN
    uint32_t region_ctrl_3 = 0;
    uint32_t limit_hi = 0;

    write_iatu_reg(iatu_base + 0x00, region_ctrl_1);
    write_iatu_reg(iatu_base + 0x04, region_ctrl_2);
    write_iatu_reg(iatu_base + 0x08, base_lo);
    write_iatu_reg(iatu_base + 0x0c, base_hi);
    write_iatu_reg(iatu_base + 0x10, limit);
    write_iatu_reg(iatu_base + 0x14, target_lo);
    write_iatu_reg(iatu_base + 0x18, target_hi);
    write_iatu_reg(iatu_base + 0x1c, limit_hi);
    write_iatu_reg(iatu_base + 0x20, region_ctrl_3);

    iatu_regions_.insert(region);

    log_debug(
        LogUMD,
        "Device: {} Mapped iATU region {} from 0x{:x} to 0x{:x} to 0x{:x}",
        pci_device_->get_device_num(),
        region,
        base,
        limit,
        target);
}

FirmwareTelemetryReader *BlackholeTTDeviceModel::get_firmware_telemetry_reader() {
    return device_firmware_->get_firmware_telemetry_reader();
}

FirmwareInfoProvider *BlackholeTTDeviceModel::get_firmware_info_provider() {
    return device_firmware_->get_firmware_info_provider();
}

SocArchDescriptor *BlackholeTTDeviceModel::get_soc_arch_descriptor() { return soc_arch_descriptor_.get(); }

ArchitectureImplementation *BlackholeTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

std::shared_ptr<SocArchDescriptor> BlackholeTTDeviceModel::get_shared_soc_arch_descriptor() {
    return soc_arch_descriptor_;
}

HangDetector *BlackholeTTDeviceModel::get_hang_detector() { return hang_detector_.get(); }

PcieInterface *BlackholeTTDeviceModel::get_pcie_interface() { return pcie_interface_; }

DmaInterface *BlackholeTTDeviceModel::get_dma_interface() { return dma_interface_; }

JtagInterface *BlackholeTTDeviceModel::get_jtag_interface() { return jtag_interface_; }

RemoteInterface *BlackholeTTDeviceModel::get_remote_interface() { return remote_interface_; }

PCIDevice *BlackholeTTDeviceModel::get_pci_device() { return pci_device_; }

}  // namespace tt::umd
