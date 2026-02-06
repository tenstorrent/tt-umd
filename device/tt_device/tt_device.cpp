// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_device.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/wormhole_tt_device.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "utils.hpp"

namespace tt::umd {

TTDevice::TTDevice(
    std::shared_ptr<PCIDevice> pci_device, std::unique_ptr<architecture_implementation> architecture_impl) :
    pci_device_(std::move(pci_device)),
    communication_device_type_(IODeviceType::PCIe),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {
    // Initialize PCIe DMA mutex through LockManager for cross-process synchronization.
    lock_manager.initialize_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);
}

TTDevice::TTDevice(
    std::shared_ptr<JtagDevice> jtag_device,
    uint8_t jlink_id,
    std::unique_ptr<architecture_implementation> architecture_impl) :
    jtag_device_(std::move(jtag_device)),
    communication_device_type_(IODeviceType::JTAG),
    communication_device_id_(jlink_id),
    architecture_impl_(std::move(architecture_impl)),
    arch(architecture_impl_->get_architecture()) {}

TTDevice::TTDevice() = default;

TTDevice::TTDevice(std::unique_ptr<architecture_implementation> architecture_impl) :
    architecture_impl_(std::move(architecture_impl)), arch(architecture_impl_->get_architecture()) {}

void TTDevice::probe_arc() {
    uint32_t dummy;
    read_from_arc_apb(&dummy, architecture_impl_->get_arc_reset_scratch_offset(), sizeof(dummy));  // SCRATCH_0
}

void TTDevice::init_tt_device(const std::chrono::milliseconds timeout_ms) {
    probe_arc();
    if (!wait_arc_core_start(timeout_ms)) {
        throw std::runtime_error(fmt::format("ARC core ({}, {}) failed to start.", arc_core.x, arc_core.y));
    }
    arc_messenger_ = ArcMessenger::create_arc_messenger(this);
    telemetry = ArcTelemetryReader::create_arc_telemetry_reader(this);
    firmware_info_provider = FirmwareInfoProvider::create_firmware_info_provider(this);
}

/* static */ std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type) {
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
            return std::unique_ptr<WormholeTTDevice>(new WormholeTTDevice(pci_device));
        case ARCH::BLACKHOLE:
            return std::unique_ptr<BlackholeTTDevice>(new BlackholeTTDevice(pci_device));
        default:
            return nullptr;
    }
}

std::unique_ptr<TTDevice> TTDevice::create(std::unique_ptr<RemoteCommunication> remote_communication) {
    switch (remote_communication->get_local_device()->get_arch()) {
        case tt::ARCH::WORMHOLE_B0: {
            // This is a workaround to allow RemoteWormholeTTDevice creation over JTAG.
            // TODO: In the future, either remove this if branch or refactor the RemoteWormholeTTDevice class hierarchy.
            if (remote_communication->get_local_device()->get_communication_device_type() == IODeviceType::JTAG) {
                return std::unique_ptr<RemoteWormholeTTDevice>(
                    new RemoteWormholeTTDevice(std::move(remote_communication), IODeviceType::JTAG));
            }
            return std::unique_ptr<RemoteWormholeTTDevice>(new RemoteWormholeTTDevice(std::move(remote_communication)));
        }
        case tt::ARCH::BLACKHOLE: {
            return nullptr;
        }
        default:
            throw std::runtime_error("Remote TTDevice creation is not supported for this architecture.");
    }
}

architecture_implementation *TTDevice::get_architecture_implementation() { return architecture_impl_.get(); }

std::shared_ptr<PCIDevice> TTDevice::get_pci_device() { return pci_device_; }

std::shared_ptr<JtagDevice> TTDevice::get_jtag_device() { return jtag_device_; }

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
    if (communication_device_type_ == IODeviceType::JTAG) {
        TT_THROW("write_regs is not applicable for JTAG communication type.");
    }
    while (word_len-- != 0) {
        *dest++ = *src++;
    }
}

TlbWindow *TTDevice::get_cached_tlb_window() {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window = std::make_unique<TlbWindow>(
            get_pci_device()->allocate_tlb(architecture_impl_->get_cached_tlb_size(), TlbMapping::UC));
        return cached_tlb_window.get();
    }
    return cached_tlb_window.get();
}

void TTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->read(communication_device_id_, mem_ptr, core.x, core.y, addr, size, is_selected_noc1() ? 1 : 0);
        return;
    }

    std::lock_guard<std::mutex> lock(tt_device_io_lock);
    get_cached_tlb_window()->read_block_reconfigure(mem_ptr, core, addr, size);
}

void TTDevice::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (communication_device_type_ == IODeviceType::JTAG) {
        jtag_device_->write(communication_device_id_, mem_ptr, core.x, core.y, addr, size, is_selected_noc1() ? 1 : 0);
        return;
    }

    std::lock_guard<std::mutex> lock(tt_device_io_lock);
    get_cached_tlb_window()->write_block_reconfigure(mem_ptr, core, addr, size);
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

void TTDevice::bar_write32(uint32_t addr, uint32_t data) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Write Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    *reinterpret_cast<volatile uint32_t *>(static_cast<uint8_t *>(get_pci_device()->bar0) + addr) = data;
}

uint32_t TTDevice::bar_read32(uint32_t addr) {
    const uint32_t bar0_offset = 0x1FD00000;
    if (addr < bar0_offset) {
        throw std::runtime_error("Read Invalid BAR address for this device.");
    }
    addr -= bar0_offset;
    return *reinterpret_cast<volatile uint32_t *>(static_cast<uint8_t *>(get_pci_device()->bar0) + addr);
}

ArcMessenger *TTDevice::get_arc_messenger() const { return arc_messenger_.get(); }

ArcTelemetryReader *TTDevice::get_arc_telemetry_reader() const { return telemetry.get(); }

FirmwareInfoProvider *TTDevice::get_firmware_info_provider() const { return firmware_info_provider.get(); }

semver_t TTDevice::get_firmware_version() { return get_firmware_info_provider()->get_firmware_version(); }

void TTDevice::wait_for_non_mmio_flush() {}

bool TTDevice::is_remote() { return is_remote_tt_device; }

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
    if (communication_device_type_ == IODeviceType::JTAG) {
        throw std::runtime_error("noc_multicast_write is not applicable for JTAG communication type.");
    }

    std::lock_guard<std::mutex> lock(tt_device_io_lock);
    get_cached_tlb_window()->noc_multicast_write_reconfigure(dst, size, core_start, core_end, addr, tlb_data::Strict);
}

void TTDevice::dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) {
    if (get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.", DeviceTypeToString.at(get_communication_device_type()));
    }

    if (get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) write.",
            get_communication_device_id());
        write_to_device(src, core, addr, size);
        return;
    }

    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);

    const uint8_t *buffer = static_cast<const uint8_t *>(src);
    PCIDevice *pci_device = get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = get_architecture_implementation()->get_static_vc();
    TlbWindow *tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base =
        get_architecture_implementation()->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id()).tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    while (size > 0) {
        auto tlb_size = tlb_window->get_size();

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        dma_h2d(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void TTDevice::dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) {
    if (get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.", DeviceTypeToString.at(get_communication_device_type()));
    }

    if (get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) read.",
            get_communication_device_id());
        read_from_device(dst, core, addr, size);
        return;
    }

    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);

    uint8_t *buffer = static_cast<uint8_t *>(dst);
    PCIDevice *pci_device = get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = get_architecture_implementation()->get_static_vc();
    TlbWindow *tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base =
        get_architecture_implementation()->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id()).tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));

    while (size > 0) {
        auto tlb_size = tlb_window->get_size();
        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        dma_d2h(buffer, axi_address, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void TTDevice::dma_multicast_write(void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    if (get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.", DeviceTypeToString.at(get_communication_device_type()));
    }

    if (get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) multicast "
            "write.",
            get_communication_device_id());

        noc_multicast_write(src, size, core_start, core_end, addr);
        return;
    }

    auto pcie_dma_lock =
        lock_manager.acquire_mutex(MutexType::PCIE_DMA, communication_device_id_, communication_device_type_);

    const uint8_t *buffer = static_cast<const uint8_t *>(src);
    PCIDevice *pci_device = get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tlb_data config{};
    config.local_offset = addr;
    config.x_start = core_start.x;
    config.y_start = core_start.y;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = get_architecture_implementation()->get_static_vc();
    config.mcast = true;
    TlbWindow *tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base =
        get_architecture_implementation()->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id()).tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    while (size > 0) {
        auto tlb_size = tlb_window->get_size();

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        dma_h2d(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

TlbWindow *TTDevice::get_cached_pcie_dma_tlb_window(tlb_data config) {
    if (cached_pcie_dma_tlb_window == nullptr) {
        cached_pcie_dma_tlb_window =
            std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(16 * 1024 * 1024, TlbMapping::WC), config);
        return cached_pcie_dma_tlb_window.get();
    }

    cached_pcie_dma_tlb_window->configure(config);
    return cached_pcie_dma_tlb_window.get();
}

void TTDevice::set_membar_flag(
    const std::vector<tt_xy_pair> &cores, const uint32_t barrier_value, const uint32_t barrier_addr) {
    tt_driver_atomics::sfence();  // Ensure that writes before this do not get reordered.
    std::vector<bool> cores_synced(cores.size(), false);
    size_t num_synced = 0;
    for (const auto &core : cores) {
        write_to_device(&barrier_value, core, barrier_addr, sizeof(uint32_t));
    }
    tt_driver_atomics::sfence();  // Ensure that all writes in the Host WC buffer are flushed.
    while (num_synced != cores.size()) {
        for (size_t i = 0; i < cores.size(); ++i) {
            if (!cores_synced[i]) {
                uint32_t readback_val;
                read_from_device(&readback_val, cores[i], barrier_addr, sizeof(std::uint32_t));
                if (readback_val == barrier_value) {
                    cores_synced[i] = true;
                    ++num_synced;
                } else {
                    log_trace(
                        LogUMD,
                        "Waiting for core {} to receive mem bar flag {} in function",
                        cores[i].str(),
                        barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned.
    tt_driver_atomics::mfence();
}

void TTDevice::insert_host_to_device_barrier(const std::vector<tt_xy_pair> &cores, const uint32_t barrier_addr) {
    // Ensure that this memory barrier is atomic across processes/threads.
    auto const lock = lock_manager.acquire_mutex(MutexType::MEM_BARRIER, pci_device_->get_device_num());
    set_membar_flag(cores, MemBarFlag::SET, barrier_addr);
    set_membar_flag(cores, MemBarFlag::RESET, barrier_addr);
}

void TTDevice::l1_membar(const std::vector<tt_xy_pair> &cores, uint32_t barrier_address, CoreType core_type) {
    if (core_type != CoreType::TENSIX && core_type != CoreType::ETH) {
        TT_THROW("l1_membar only supports TENSIX and ETH core types at TTDevice level.");
    }

    if (cores.empty()) {
        // When no cores specified, cannot perform barrier as we don't have soc_descriptor at TTDevice level.
        // The caller should use Chip-level API for full barrier on all cores.
        TT_THROW("l1_membar with empty cores set is not supported at TTDevice level. Use Chip-level API instead.");
    }

    // Insert barrier on specific cores with L1.
    insert_host_to_device_barrier(cores, barrier_address);
}

}  // namespace tt::umd
