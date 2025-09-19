// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/tt_device.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/lock_manager.hpp"

// TODO #526: This is a hack to allow UMD to use the NOC1 TLB.
bool umd_use_noc1 = false;

namespace tt::umd {

void TTDevice::use_noc1(bool use_noc1) { umd_use_noc1 = use_noc1; }

TTDevice::TTDevice(
    std::shared_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl) :
    pci_device_(pci_device),
    communication_device_type_(IODeviceType::PCIe),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {}

TTDevice::TTDevice(
    std::shared_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    std::unique_ptr<architecture_implementation> architecture_impl) :
    jtag_device_(jtag_device),
    jlink_id_(jlink_id),
    communication_device_type_(IODeviceType::JTAG),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {}

void TTDevice::init_tt_device() {
    pre_init_hook();
    arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
    firmware_info_provider = FirmwareInfoProvider::create_firmware_info_provider(this);
    wait_arc_core_start();
    post_init_hook();
}

TTDevice::TTDevice() {}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type) {
    // TODO make abstract IO handler inside TTDevice.
    if (device_type == IODeviceType::JTAG) {
        auto jtag_device = JtagDevice::create();

        switch (jtag_device->get_jtag_arch(device_number)) {
            case ARCH::WORMHOLE_B0:
                return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(jtag_device, device_number));
            case ARCH::BLACKHOLE:
                TT_THROW("JTAG is not yet supported on Blackhole architecture.");
            default:
                return nullptr;
        }
    }

    auto pci_device = std::make_shared<PCIDevice>(device_number);

    switch (pci_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(pci_device));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(pci_device));
        default:
            return nullptr;
    }
}

std::unique_ptr<TTDevice> TTDevice::create(
    std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip) {
    switch (remote_communication->get_local_device()->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            return std::unique_ptr<RemoteWormholeTTDevice>(
                new RemoteWormholeTTDevice(std::move(remote_communication), target_chip));
        }
        case tt::ARCH::BLACKHOLE: {
            return std::unique_ptr<RemoteBlackholeTTDevice>(
                new RemoteBlackholeTTDevice(std::move(remote_communication)));
        }
        default:
            throw std::runtime_error("Remote TTDevice creation is not supported for this architecture.");
    }
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

std::shared_ptr<PCIDevice> TTDevice::get_pci_device() { return pci_device_; }

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

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc
void TTDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_regs is not applicable for JTAG communication type.");
    }
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

TlbWindow *TTDevice::get_cached_tlb_window(tlb_data config) {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window =
            std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(1 << 21, TlbMapping::UC), config);
        return cached_tlb_window.get();
    }

    cached_tlb_window->configure(config);
    return cached_tlb_window.get();
}

void TTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(tt_device_io_lock);

    uint8_t *buffer_addr = static_cast<uint8_t *>(mem_ptr);
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Strict;
    config.static_vc = (get_arch() == tt::ARCH::BLACKHOLE) ? false : true;
    TlbWindow *tlb_window = get_cached_tlb_window(config);

    while (size > 0) {
        uint32_t tlb_size = tlb_window->get_size();
        uint32_t transfer_size = std::min(size, tlb_size);

        tlb_window->read_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
    }
}

void TTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(jlink_id_, mem_ptr, core.x, core.y, addr, size);
        return;
    }
    std::lock_guard<std::mutex> lock(tt_device_io_lock);

    uint8_t *buffer_addr = (uint8_t *)(uintptr_t)mem_ptr;
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Strict;
    config.static_vc = (get_arch() == tt::ARCH::BLACKHOLE) ? false : true;
    TlbWindow *tlb_window = get_cached_tlb_window(config);

    while (size > 0) {
        uint32_t tlb_size = tlb_window->get_size();

        uint32_t transfer_size = std::min(size, tlb_size);

        tlb_window->write_block(0, buffer_addr, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
    }
}

void TTDevice::configure_iatu_region(size_t region, uint64_t target, size_t region_size) {
    throw std::runtime_error("configure_iatu_region is not implemented for this device");
}

void TTDevice::wait_dram_channel_training(const uint32_t dram_channel, const uint32_t timeout_ms) {
    if (dram_channel >= architecture_impl_->get_dram_banks_number()) {
        throw std::runtime_error(fmt::format(
            "Invalid DRAM channel index {}, maximum index for given architecture is {}",
            dram_channel,
            architecture_impl_->get_dram_banks_number() - 1));
    }
    auto start = std::chrono::system_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status = get_dram_training_status();

        if (dram_training_status.empty()) {
            log_warning(
                LogSiliconDriver, "DRAM training status is not available, breaking the wait for DRAM training.");
            return;
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::FAIL) {
            throw std::runtime_error("DRAM training failed");
        }

        if (dram_training_status.at(dram_channel) == DramTrainingStatus::SUCCESS) {
            return;
        }

        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms) {
            throw std::runtime_error(
                fmt::format("DRAM training for channel {} timed out after {} ms", dram_channel, timeout_ms));
            break;
        }
    }
}

void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Write Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    *reinterpret_cast<volatile uint32_t *>((uint8_t *)get_pci_device()->bar0 + addr) = data;
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Read Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    return *reinterpret_cast<volatile uint32_t *>((uint8_t *)get_pci_device()->bar0 + addr);
}

ArcMessenger *TTDevice::get_arc_messenger() const { return arc_messenger_.get(); }

ArcTelemetryReader *TTDevice::get_arc_telemetry_reader() const { return telemetry.get(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider.get(); }

semver_t TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return is_remote_tt_device; }

int TTDevice::get_communication_device_id() const {
    return communication_device_type_ == IODeviceType::PCIe ? pci_device_->get_device_num()
                                                            : jtag_device_->get_device_id(jlink_id_);
}

IODeviceType TTDevice::get_communication_device_type() const { return communication_device_type_; }

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

uint64_t TTDevice::get_refclk_counter() {
    uint32_t high1_addr = 0, high2_addr = 0, low_addr = 0;
    read_from_arc(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    read_from_arc(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    read_from_arc(&high1_addr, architecture_impl_->get_arc_reset_unit_refclk_high_offset(), sizeof(high1_addr));
    if (high2_addr > high1_addr) {
        read_from_arc(&low_addr, architecture_impl_->get_arc_reset_unit_refclk_low_offset(), sizeof(low_addr));
    }
    return (static_cast<uint64_t>(high2_addr) << 32) | low_addr;
}

uint64_t TTDevice::get_board_id() { return get_firmware_info_provider()->get_board_id(); }

std::vector<DramTrainingStatus> TTDevice::get_dram_training_status() {
    if (!telemetry->is_entry_available(TelemetryTag::DDR_STATUS)) {
        return {};
    }

    std::vector<DramTrainingStatus> dram_training_status;
    const uint32_t num_dram_channels = architecture_impl_->get_dram_banks_number();
    // Format of the dram training status is as follows:
    // Each channel gets two bits in the 32-bit value (16 bits used). The lower bits are for lower channels.
    // Lower of the two bits is for training error and higher of the two bits is for training status.
    // Example: 0b 00 00 00 00 00 00 01 10
    // would mean that only channel 0 is trained, channel 1 has the error and other are not trained and don't have
    // errors. If some channel is harvested the bits are always going to be zero.
    for (uint32_t dram_channel = 0; dram_channel < num_dram_channels; dram_channel++) {
        dram_training_status.push_back(get_firmware_info_provider()->get_dram_training_status(dram_channel));
    }

    return dram_training_status;
}

double TTDevice::get_asic_temperature() { return get_firmware_info_provider()->get_asic_temperature(); }

uint8_t TTDevice::get_asic_location() { return get_firmware_info_provider()->get_asic_location(); }

ChipInfo TTDevice::get_chip_info() {
    ChipInfo chip_info;

    chip_info.noc_translation_enabled = get_noc_translation_enabled();
    chip_info.chip_uid.board_id = get_board_id();
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

}  // namespace tt::umd
