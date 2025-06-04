// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/tt_device.h"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arc_messenger.h"
#include "umd/device/driver_atomics.h"
#include "umd/device/tt_device/blackhole_tt_device.h"
#include "umd/device/tt_device/tlb_window.h"
#include "umd/device/tt_device/wormhole_tt_device.h"

// TODO #526: This is a hack to allow UMD to use the NOC1 TLB.
bool umd_use_noc1 = false;

namespace tt::umd {

void TTDevice::use_noc1(bool use_noc1) { umd_use_noc1 = use_noc1; }

TTDevice::TTDevice(
    std::shared_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl) :
    pci_device_(pci_device),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    lock_manager.initialize_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
}

void TTDevice::init_tt_device() {
    arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
}

TTDevice::TTDevice() {}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(int pci_device_number) {
    auto pci_device = std::make_shared<PCIDevice>(pci_device_number);

    switch (pci_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeTTDevice>(pci_device);
        case ARCH::BLACKHOLE:
            return std::make_unique<BlackholeTTDevice>(pci_device);
        default:
            return nullptr;
    }
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

std::shared_ptr<PCIDevice> TTDevice::get_pci_device() { return pci_device_; }

tt::ARCH TTDevice::get_arch() { return arch; }

bool TTDevice::is_hardware_hung() {
    uint32_t scratch_data = bar_read32(architecture_impl_->get_arc_reset_scratch_offset() + 6 * 4);

    return (scratch_data == HANG_READ_VALUE);
}

void TTDevice::detect_hang_read(std::uint32_t data_read) {
    if (data_read == HANG_READ_VALUE && is_hardware_hung()) {
        throw std::runtime_error("Read 0xffffffff from PCIE: you should reset the board.");
    }
}

// This is only needed for the BH workaround in iatu_configure_peer_region since no arc
void TTDevice::write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) {
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

void TTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    uint8_t *buffer_addr = static_cast<uint8_t *>(mem_ptr);
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = (get_arch() == tt::ARCH::BLACKHOLE) ? false : true;
    const uint32_t two_mb_size = 1 << 21;
    std::unique_ptr<TlbWindow> tlb_window =
        std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(two_mb_size, TlbMapping::WC), config);
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
    auto lock = lock_manager.acquire_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num());
    uint8_t *buffer_addr = (uint8_t *)(uintptr_t)mem_ptr;
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = (get_arch() == tt::ARCH::BLACKHOLE) ? false : true;
    const uint32_t two_mb_size = 1 << 21;
    std::unique_ptr<TlbWindow> tlb_window =
        std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(two_mb_size, TlbMapping::WC), config);

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

void TTDevice::wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms) {
    throw std::runtime_error("Waiting for ARC core to start is supported only for Blackhole TTDevice.");
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

TTDevice::~TTDevice() { lock_manager.clear_mutex(MutexType::TT_DEVICE_IO, get_pci_device()->get_device_num()); }

std::vector<DramTrainingStatus> TTDevice::get_dram_training_status() { return {}; }

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return is_remote_tt_device; }

BoardType TTDevice::get_board_type() { return get_board_type_from_board_id(get_board_id()); }

semver_t TTDevice::fw_version_from_telemetry(const uint32_t telemetry_data) const {
    // The telemetry data is a 32-bit value where the higher 16 bits are the major value,
    // lower 16 bits are the minor value.
    uint16_t major = (telemetry_data >> 24) & 0xFF;
    uint16_t minor = (telemetry_data >> 16) & 0xFF;
    return semver_t(major, minor, 0);
}

}  // namespace tt::umd
