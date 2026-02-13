// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_sim_tt_device.hpp"

#include <filesystem>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "simulation_device_generated.h"
#include "umd/device/simulation/simulation_chip.hpp"

namespace tt::umd {

static const uint16_t WH_PCIE_DEVICE_ID = 0x401e;
static const uint16_t BH_PCIE_DEVICE_ID = 0xb140;

static_assert(!std::is_abstract<TTSimTTDevice>(), "TTSimChip must be non-abstract.");

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create(const std::filesystem::path& simulator_directory) {
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    SocDescriptor soc_descriptor = SocDescriptor(soc_desc_path);
    return std::make_unique<TTSimTTDevice>(simulator_directory, soc_descriptor, 0);
}

TTSimTTDevice::TTSimTTDevice(
    const std::filesystem::path& simulator_directory,
    SocDescriptor soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary) :
    communicator_(std::make_unique<TTSimCommunicator>(simulator_directory, copy_sim_binary)),
    simulator_directory_(simulator_directory),
    soc_descriptor_(std::move(soc_descriptor)),
    chip_id_(chip_id),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)) {
    communicator_->initialize();
    // Read the PCI ID (first 32 bits of PCI config space).
    uint32_t pci_id = communicator_->pci_config_read32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = communicator_->pci_config_read32(0, 0) >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");

    if ((libttsim_pci_device_id == WH_PCIE_DEVICE_ID) || (libttsim_pci_device_id == BH_PCIE_DEVICE_ID)) {
        // Compute physical address of BAR0 from PCI config registers.
        bar0_base = communicator_->pci_config_read32(0, 0x10);
        bar0_base |= uint64_t(communicator_->pci_config_read32(0, 0x14)) << 32;
        bar0_base &= ~15ull;  // ignore attributes, just obtain the physical address

        if (libttsim_pci_device_id == WH_PCIE_DEVICE_ID) {
            tlb_region_size_ = 16 * 1024 * 1024;
        } else {
            tlb_region_size_ = 2 * 1024 * 1024;
        }
    }
}

TTSimTTDevice::~TTSimTTDevice() = default;

void TTSimTTDevice::start_device() {}

void TTSimTTDevice::close_device() { communicator_->shutdown(); }

void TTSimTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogUMD, "Device writing {} bytes to l1_dest {} in core {}", size, addr, core.str());
    if (tlb_region_size_) {  // if set, split into requests that do not span TLB regions
        while (size) {
            uint32_t cur_size = std::min(size, tlb_region_size_ - uint32_t(addr & (tlb_region_size_ - 1)));
            communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, cur_size);
            addr += cur_size;
            mem_ptr = reinterpret_cast<const uint8_t*>(mem_ptr) + cur_size;
            size -= cur_size;
        }
    } else {
        communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
    }
}

void TTSimTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (tlb_region_size_) {  // if set, split into requests that do not span TLB regions
        while (size) {
            uint32_t cur_size = std::min(size, tlb_region_size_ - uint32_t(addr & (tlb_region_size_ - 1)));
            communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, cur_size);
            addr += cur_size;
            mem_ptr = reinterpret_cast<uint8_t*>(mem_ptr) + cur_size;
            size -= cur_size;
        }
    } else {
        communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
    }
    communicator_->advance_clock(10);
}

void TTSimTTDevice::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if ((libttsim_pci_device_id == WH_PCIE_DEVICE_ID) || (libttsim_pci_device_id == BH_PCIE_DEVICE_ID)) {
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint32_t reset_value = uint32_t(soft_resets);
        write_to_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
    } else if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint64_t reset_value = uint64_t(soft_resets);
        if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
            reset_value = 0xF0000;  // This is using old API, translate to QSR values
        } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
            reset_value = 0xFFF00;  // This is using old API, translate to QSR values
        }
        write_to_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
    } else {
        TT_THROW("Missing implementation of reset for this chip.");
    }
}

void TTSimTTDevice::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    for (const tt_xy_pair core : soc_descriptor_.get_cores(CoreType::TENSIX)) {
        send_tensix_risc_reset(core, soft_resets);
    }
}

void TTSimTTDevice::assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value &=
            ~(uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value |= soft_reset_update;
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    }
}

void TTSimTTDevice::deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);

    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value |=
            (uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        read_from_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
        reset_value &= ~soft_reset_update;
        write_to_device(&reset_value, core, soft_reset_addr, sizeof(reset_value));
    }
}

void TTSimTTDevice::dma_d2h(void* dst, uint32_t src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d(uint32_t dst, const void* src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC APB access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC APB access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC CSM access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC CSM access is not supported in TTSim simulation device.");
}

bool TTSimTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    throw std::runtime_error("Waiting for ARC core start is not supported in TTSim simulation device.");
}

std::chrono::milliseconds TTSimTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    throw std::runtime_error("Waiting for ETH core training is not supported in TTSim simulation device.");
}

EthTrainingStatus TTSimTTDevice::read_eth_core_training_status(tt_xy_pair eth_core) {
    throw std::runtime_error("Reading ETH core training status is not supported in TTSim simulation device.");
}

uint32_t TTSimTTDevice::get_clock() {
    throw std::runtime_error("Getting clock is not supported in TTSim simulation device.");
}

uint32_t TTSimTTDevice::get_min_clock_freq() {
    throw std::runtime_error("Getting minimum clock frequency is not supported in TTSim simulation device.");
}

bool TTSimTTDevice::get_noc_translation_enabled() {
    throw std::runtime_error("Getting NOC translation status is not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    throw std::runtime_error("DMA multicast write not supported for TTSim simulation device.");
}

}  // namespace tt::umd
