// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_device.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "assert.hpp"
#include "noc_access.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/protocol/jtag_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_protocol.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/telemetry.hpp"
#include "utils.hpp"

namespace tt::umd {

/* static */ void TTDevice::set_sigbus_safe_handler(bool set_safe_handler) {
    TlbWindow::set_sigbus_safe_handler(set_safe_handler);
}

TTDevice::TTDevice(
    std::shared_ptr<PCIDevice> pci_device,
    std::unique_ptr<architecture_implementation> architecture_impl,
    bool use_safe_api) :
    pci_device_(std::move(pci_device)),
    communication_device_type_(IODeviceType::PCIe),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    auto pcie_protocol = std::make_unique<PcieProtocol>(pci_device_, architecture_impl_.get(), use_safe_api);
    pcie_capabilities_ = pcie_protocol.get();
    mmio_protocol_ = pcie_protocol.get();
    device_protocol_ = std::move(pcie_protocol);
    if (use_safe_api) {
        set_sigbus_safe_handler(true);
    }
}

TTDevice::TTDevice(
    std::shared_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    std::unique_ptr<architecture_implementation> architecture_impl) :
    jtag_device_(std::move(jtag_device)),
    communication_device_type_(IODeviceType::JTAG),
    communication_device_id_(jlink_id),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    auto jtag_protocol =
        std::make_unique<JtagProtocol>(jtag_device_, communication_device_id_, architecture_impl.get());
    mmio_protocol_ = jtag_protocol.get();
    jtag_capabilities_ = jtag_protocol.get();
    device_protocol_ = std::move(jtag_protocol);
}

TTDevice::TTDevice(
    std::unique_ptr<RemoteCommunication> remote_communication,
    std::unique_ptr<architecture_implementation> architecture_impl) :
    architecture_impl_(std::move(architecture_impl)) {
    auto remote_protocol = std::make_unique<RemoteProtocol>(std::move(remote_communication));
    remote_capabilites_ = remote_protocol.get();
    device_protocol_ = std::move(remote_protocol);
    is_remote_tt_device_ = true;
}

TTDevice::TTDevice() = default;

TTDevice::TTDevice(std::unique_ptr<architecture_implementation> architecture_impl) :
    architecture_impl_(std::move(architecture_impl)), arch(architecture_impl_->get_architecture()) {}

void TTDevice::probe_arc() {
    uint32_t dummy;
    read_from_arc_apb(&dummy, architecture_impl_->get_arc_reset_scratch_offset(), sizeof(dummy));  // SCRATCH_0
}

TTDeviceInitResult TTDevice::init_tt_device(const std::chrono::milliseconds timeout_ms, bool throw_on_arc_failure) {
    probe_arc();
    if (!wait_arc_core_start(timeout_ms)) {
        if (throw_on_arc_failure) {
            throw std::runtime_error(fmt::format("ARC core ({}, {}) failed to start.", arc_core.x, arc_core.y));
        } else {
            return TTDeviceInitResult::ARC_STARTUP_FAILED;
        }
    }
    try {
        arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    } catch (const std::runtime_error &e) {
        return TTDeviceInitResult::ARC_MESSENGER_UNAVAILABLE;
    }
    try {
        telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
    } catch (const std::runtime_error &e) {
        return TTDeviceInitResult::ARC_TELEMETRY_UNAVAILABLE;
    }
    try {
        firmware_info_provider = FirmwareInfoProvider::create_firmware_info_provider(this);
    } catch (const std::runtime_error &e) {
        return TTDeviceInitResult::FIRMWARE_INFO_PROVIDER_UNAVAILABLE;
    }
    return TTDeviceInitResult::SUCCESSFUL;
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(
    int device_number, IODeviceType device_type, bool use_safe_api) {
    // TODO make abstract IO handler inside TTDevice.
    if (device_type == IODeviceType::JTAG) {
        auto jtag_device = JtagDevice::create();
        switch (jtag_device->get_jtag_arch(device_number)) {
            case ARCH::WORMHOLE_B0:
                return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(jtag_device, device_number));
            case ARCH::BLACKHOLE:
                return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(jtag_device, device_number));
            default:
                return nullptr;
        }
    }

    auto pci_device = std::make_shared<PCIDevice>(device_number);

    switch (pci_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(pci_device, use_safe_api));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(pci_device, use_safe_api));
        default:
            return nullptr;
    }
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(std::unique_ptr<RemoteCommunication> remote_communication) {
    switch (remote_communication->get_mmio_protocol()->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(std::move(remote_communication)));
        }
        case tt::ARCH::BLACKHOLE: {
            throw std::runtime_error("Remote TTDevice creation is not supported for Blackhole.");
        }
        default:
            throw std::runtime_error("Remote TTDevice creation is not supported for this architecture.");
    }
}

PcieInterface *TTDevice::get_pcie_interface() {
    if (pcie_capabilities_ == nullptr) {
        throw std::runtime_error("TTDevice was built with a non-PCIe protocol.");
    }
    return pcie_capabilities_;
}

JtagInterface *TTDevice::get_jtag_interface() {
    if (jtag_capabilities_ == nullptr) {
        throw std::runtime_error("TTDevice was built with a non-JTAG protocol.");
    }
    return jtag_capabilities_;
}

RemoteInterface *TTDevice::get_remote_interface() {
    if (remote_capabilites_ == nullptr) {
        throw std::runtime_error("TTDevice was built with a non-Remote protocol.");
    }
    return remote_capabilites_;
}

MmioProtocol *TTDevice::get_mmio_protocol() {
    if (mmio_protocol_ == nullptr) {
        throw std::runtime_error("TTDevice was built with a Remote protocol.");
    }
    return mmio_protocol_;
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

PCIDevice *TTDevice::get_pci_device() { return get_pcie_interface()->get_pci_device(); }

JtagDevice *TTDevice::get_jtag_device() { return get_jtag_interface()->get_jtag_device(); }

RemoteCommunication *TTDevice::get_remote_communication() { return get_remote_interface()->get_remote_communication(); }

tt::ARCH TTDevice::get_arch() { return arch; }

void TTDevice::detect_hang_read(std::uint32_t data_read) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        // Jtag protocol uses different communication paths from pci therefore
        // there's no need to check hang which is in this case pci-specific.
        return;
    }
    if (data_read == HANG_READ_VALUE && is_hardware_hung()) {
        throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
    }
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc.
void TTDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    get_pcie_interface()->write_regs(dest, src, word_len);
}

void TTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    device_protocol_->read_from_device(mem_ptr, core, addr, size);
}

void TTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    device_protocol_->write_to_device(mem_ptr, core, addr, size);
}

void TTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    throw std::runtime_error("configure_iatu_region is not implemented for this device");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const std::chrono::milliseconds timeout_ms) {
    if (dram_channel >= architecture_impl_->get_dram_banks_number()) {
        throw std::runtime_error(fmt::format(
            "Invalid DRAM channel index {}, maximum index for given architecture is {}",
            dram_channel,
            architecture_impl_->get_dram_banks_number() - 1));
    }
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status =
            get_firmware_info_provider()->get_dram_training_status(architecture_impl_->get_dram_banks_number());

        if (dram_training_status.empty()) {
            log_warning(LogUMD, "DRAM training status is not available, breaking the wait for DRAM training.");
            return;
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            throw std::runtime_error("DRAM training failed");
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::SUCCESS) {
            return;
        }

        utils::check_timeout(
            start,
            timeout_ms,
            fmt::format("DRAM training for channel {} timed out after {} ms", dram_channel, timeout_ms));
    }
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) { get_pcie_interface()->bar_write32(addr, data); }

uint32_t TTDevice::bar_read32(uint32_t addr) { return get_pcie_interface()->bar_read32(addr); }

ArcMessenger *TTDevice::get_arc_messenger() const { return arc_messenger_.get(); }

ArcTelemetryReader *TTDevice::get_arc_telemetry_reader() const { return telemetry.get(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider.get(); }

semver_t TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return is_remote_tt_device_; }

int TTDevice::get_communication_device_id() const { return communication_device_id_; }

IODeviceType TTDevice::get_communication_device_type() const { return communication_device_type_; }

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter() {
    uint32_t high1_addr = 0;
    uint32_t high2_addr = 0;
    uint32_t low_addr = 0;
    read_from_arc_apb(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    read_from_arc_apb(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    read_from_arc_apb(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    if (high2_addr > high1_addr) {
        read_from_arc_apb(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

uint64_t TTDevice::get_board_id() { return get_firmware_info_provider()->get_board_id(); }

double TTDevice::get_asic_temperature() { return get_firmware_info_provider()->get_asic_temperature(); }

uint8_t TTDevice::get_asic_location() { return get_firmware_info_provider()->get_asic_location(); }

ChipInfo TTDevice::get_chip_info() {
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled();
    chip_info.board_id = get_board_id();
    chip_info.board_type = get_board_type();
    chip_info.asic_location = get_asic_location();

    return chip_info;
}

uint32_t TTDevice::get_max_clock_freq() { return get_firmware_info_provider()->get_max_clock_freq(); }

uint32_t TTDevice::get_risc_reset_state(tt_xy_pair core) {
    uint32_t tensix_risc_state;
    read_from_device(&tensix_risc_state, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));

    return tensix_risc_state;
}

void TTDevice::set_risc_reset_state(tt_xy_pair core, const uint32_t risc_flags) {
    write_to_device(&risc_flags, core, architecture_impl_->get_tensix_soft_reset_addr(), sizeof(uint32_t));
    tt_driver_atomics::sfence();
}

tt_xy_pair TTDevice::get_arc_core() const { return arc_core; }

void TTDevice::noc_multicast_write(void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    get_pcie_interface()->noc_multicast_write(dst, size, core_start, core_end, addr);
}

void TTDevice::dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) {
    get_pcie_interface()->dma_write_to_device(src, size, core, addr);
}

void TTDevice::dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) {
    get_pcie_interface()->dma_read_from_device(dst, size, core, addr);
}

void TTDevice::dma_multicast_write(void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    get_pcie_interface()->dma_multicast_write(src, size, core_start, core_end, addr);
}

void TTDevice::dma_d2h(void *dst, uint32_t src, size_t size) { get_pcie_interface()->dma_d2h(dst, src, size); }

void TTDevice::dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) {
    get_pcie_interface()->dma_d2h_zero_copy(dst, src, size);
}

void TTDevice::dma_h2d(uint32_t dst, const void *src, size_t size) { get_pcie_interface()->dma_h2d(dst, src, size); }

void TTDevice::dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) {
    get_pcie_interface()->dma_h2d_zero_copy(dst, src, size);
}

}  // namespace tt::umd
