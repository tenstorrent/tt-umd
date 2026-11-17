// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/pcie_protocol.hpp"

#include <fmt/format.h>
#include <unistd.h>

#include <stdexcept>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "common/utils.hpp"
#include "pcie/io_window_reconfigure.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// PCIDevice maps the top bar0_size bytes of the 512 MiB BAR0, the part past the TLB windows that holds
// the register space.
constexpr uint64_t BAR0_SIZE = 512ULL << 20;
constexpr uint64_t BAR0_REGISTER_SPACE_START = BAR0_SIZE - PCIDevice::bar0_size;

}  // namespace

PcieProtocol::PcieProtocol(
    std::unique_ptr<PCIDevice> pci_device, ArchitectureImplementation* architecture_impl, IoSafety io_safety) :
    pci_device_(std::move(pci_device)), architecture_impl_(architecture_impl), io_safety_(io_safety) {
    if (io_safety_ == IoSafety::Enabled) {
        SiliconTlbWindow::set_sigbus_safe_handler(true);
    }
}

PcieProtocol::~PcieProtocol() = default;

int PcieProtocol::get_mmio_id() { return pci_device_->get_device_num(); }

int PcieProtocol::get_numa_node() const { return pci_device_->get_numa_node(); }

std::optional<uint32_t> PcieProtocol::get_lane_count() const { return pci_device_->get_current_link_width(); }

PCIDevice* PcieProtocol::get_pci_device() { return pci_device_.get(); }

void PcieProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(data_window_.mutex);
    read_block_reconfigure(get_shared_window(data_window_, TlbMapping::WC), dst, core, addr, size, noc_id);
}

void PcieProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(data_window_.mutex);
    write_block_reconfigure(get_shared_window(data_window_, TlbMapping::WC), src, core, addr, size, noc_id);
}

void PcieProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(pci_device_->get_arch(), width);
    validate_register_access(addr, size, width);
    std::lock_guard<std::mutex> lock(ctrl_window_.mutex);
    read_register_reconfigure(get_shared_window(ctrl_window_, TlbMapping::UC), dst, core, addr, size, noc_id, width);
}

void PcieProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(pci_device_->get_arch(), width);
    validate_register_access(addr, size, width);
    std::lock_guard<std::mutex> lock(ctrl_window_.mutex);
    write_register_reconfigure(get_shared_window(ctrl_window_, TlbMapping::UC), src, core, addr, size, noc_id, width);
}

bool PcieProtocol::write_to_core_range(
    const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(data_window_.mutex);
    noc_multicast_write_reconfigure(
        get_shared_window(data_window_, TlbMapping::WC), src, size, core_start, core_end, addr, noc_id);
    return true;
}

volatile uint32_t* PcieProtocol::get_bar_register(uint32_t addr) const {
    if (addr % sizeof(uint32_t) != 0 || addr < BAR0_REGISTER_SPACE_START ||
        addr - BAR0_REGISTER_SPACE_START + sizeof(uint32_t) > PCIDevice::bar0_size) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "BAR0 address {:#x} is not a 4-byte aligned address in the register space [{:#x}, {:#x}).",
                addr,
                BAR0_REGISTER_SPACE_START,
                BAR0_SIZE));
    }
    return reinterpret_cast<volatile uint32_t*>(
        static_cast<uint8_t*>(pci_device_->bar0) + (addr - BAR0_REGISTER_SPACE_START));
}

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) { *get_bar_register(addr) = data; }

uint32_t PcieProtocol::bar_read32(uint32_t addr) { return *get_bar_register(addr); }

void PcieProtocol::set_power_state(PowerState state) { pci_device_->set_power_state(state == PowerState::HIGH); }

int PcieProtocol::export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    const uint64_t page_size = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
    if (size == 0 || addr % page_size != 0 || size % page_size != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Cannot export {:#x} bytes at {:#x}: address and size must be page aligned and the size non-zero.",
                size,
                addr));
    }

    // A window maps a size-aligned base, so the export starts addr's offset from that base into it.
    for (const TlbSizeClass& size_class : get_architecture_tlbs(architecture_impl_->get_architecture()).size_classes) {
        const uint64_t offset_in_window = addr % size_class.size;
        if (offset_in_window + size > size_class.size) {
            continue;
        }
        tlb_data config{};
        config.local_offset = addr - offset_in_window;
        config.x_end = core.x;
        config.y_end = core.y;
        config.noc_sel = static_cast<uint64_t>(noc_id);
        config.ordering = ordering;
        return pci_device_->export_tlb_dmabuf(size_class.size, config, offset_in_window, size);
    }
    UMD_THROW(
        error::RuntimeError, fmt::format("No TLB window size can cover {:#x} bytes at {:#x} for export.", size, addr));
}

void PcieProtocol::set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) {
    {
        std::lock_guard<std::mutex> lock(hang_check_mutex_);
        io_timeout_hang_check_ = hang_check;
    }
    for (SharedWindow* shared : {&data_window_, &ctrl_window_}) {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->window != nullptr) {
            shared->window->set_io_timeout_hang_check(hang_check);
        }
    }
}

std::unique_ptr<TlbWindow> PcieProtocol::allocate_window(tlb_data config, TlbMapping mapping, size_t size) {
    for (const TlbSizeClass& size_class : get_architecture_tlbs(architecture_impl_->get_architecture()).size_classes) {
        if (size_class.size < size) {
            continue;
        }
        std::unique_ptr<TlbHandle> handle;
        try {
            handle = pci_device_->allocate_tlb(size_class.size, mapping);
        } catch (const std::runtime_error& e) {
            // Every window of this size may be taken; a larger one serves the request as well.
            log_debug(tt::LogUMD, "{}", e.what());
            continue;
        }
        auto window = std::make_unique<SiliconTlbWindow>(std::move(handle), config, io_safety_);
        std::lock_guard<std::mutex> lock(hang_check_mutex_);
        window->set_io_timeout_hang_check(io_timeout_hang_check_);
        return window;
    }
    UMD_THROW(
        error::RuntimeError,
        fmt::format("No TLB window of at least {:#x} bytes can be allocated on PCIe device {}.", size, get_mmio_id()));
}

TlbWindow& PcieProtocol::get_shared_window(SharedWindow& shared, TlbMapping mapping) {
    if (shared.window == nullptr) {
        shared.window = allocate_window(
            tlb_data{}, mapping, get_architecture_tlbs(architecture_impl_->get_architecture()).cached_window_size);
    }
    return *shared.window;
}

}  // namespace tt::umd
