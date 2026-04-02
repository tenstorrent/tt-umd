// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_sim_tt_device.hpp"

#include <filesystem>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "simulation_device_generated.h"
#include "umd/device/pcie/pci_ids.h"
#include "umd/device/pcie/tt_sim_tlb_handle.hpp"
#include "umd/device/pcie/tt_sim_tlb_window.hpp"
#include "umd/device/simulation/simulation_chip.hpp"

namespace tt::umd {

namespace {

/**
 * TTSim-specific TLBManager that creates TTSimTlbHandle + TTSimTlbWindow directly,
 * eliminating the need for a std::function factory.
 */
class TTSimTLBManager : public TLBManager {
public:
    TTSimTLBManager(TTDevice* tt_device, TlbAllocator* allocator, TTSimCommunicator* communicator) :
        TLBManager(tt_device), allocator_(allocator), communicator_(communicator) {}

    void configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) override {
        log_debug(LogUMD, "Requesting TLB window of size {}", tlb_size);

        tlb_data config = build_tlb_config(core, address, ordering);

        const auto* arch_impl = allocator_->get_architecture_impl();
        if (arch_impl->get_architecture() == tt::ARCH::WORMHOLE_B0 &&
            (tlb_size == arch_impl->get_cached_tlb_size() || tlb_size == arch_impl->get_dynamic_tlb_2m_size())) {
            throw std::runtime_error("TLBs of 1MB and 2MB are not supported in simulation for Wormhole architecture.");
        }

        int tlb_index = allocator_->allocate_tlb_index(tlb_size);
        if (tlb_index == -1) {
            throw std::runtime_error("No available TLB of requested size");
        }

        size_t actual_tlb_size = allocator_->get_tlb_size_from_index(tlb_index);
        auto handle = TTSimTlbHandle::create(allocator_, communicator_, tlb_index, actual_tlb_size, TlbMapping::WC);
        auto window = std::make_unique<TTSimTlbWindow>(std::move(handle), communicator_, config);

        register_tlb_window(core, tlb_size, address, std::move(window));
    }

    std::unique_ptr<TlbWindow> allocate_tlb_window(tlb_data config, TlbMapping mapping, size_t tlb_size) {
        int tlb_index = allocator_->allocate_tlb_index(tlb_size);
        if (tlb_index == -1) {
            throw std::runtime_error("No available TLB of requested size");
        }

        size_t actual_tlb_size = allocator_->get_tlb_size_from_index(tlb_index);
        auto handle = TTSimTlbHandle::create(allocator_, communicator_, tlb_index, actual_tlb_size, mapping);
        return std::make_unique<TTSimTlbWindow>(std::move(handle), communicator_, config);
    }

private:
    TlbAllocator* allocator_;
    TTSimCommunicator* communicator_;
};

}  // namespace

static_assert(!std::is_abstract<TTSimTTDevice>(), "TTSimChip must be non-abstract.");

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create(
    const std::filesystem::path& simulator_directory, int num_host_mem_channels, bool copy_sim_binary) {
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    tt::ARCH arch = SocDescriptor::get_arch_from_soc_descriptor_path(soc_desc_path);
    ChipInfo chip_info{};
    if (arch == tt::ARCH::BLACKHOLE) {
        // We need to set this default harvesting mask for Blackhole so we could create SocDescriptor.
        // We have the same code in creating mock cluster descriptor, but this code is supposed to be used.
        // without creating ClusterDescriptor, so we need to add it here as well.
        chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
    }
    SocDescriptor soc_descriptor = SocDescriptor(soc_desc_path, chip_info);
    return std::make_unique<TTSimTTDevice>(
        simulator_directory, soc_descriptor, 0, copy_sim_binary, num_host_mem_channels);
}

TTSimTTDevice::TTSimTTDevice(
    const std::filesystem::path& simulator_directory,
    SocDescriptor soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary,
    int num_host_mem_channels) :
    communicator_(std::make_unique<TTSimCommunicator>(simulator_directory, copy_sim_binary)),
    simulator_directory_(simulator_directory),
    soc_descriptor_(std::move(soc_descriptor)),
    chip_id_(chip_id),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)),
    sysmem_manager_(std::make_unique<SimulationSysmemManager>(num_host_mem_channels, soc_descriptor_.arch)) {
    communicator_->initialize();
    initialize_sysmem_functions();
    communicator_->start_sim();
    // Read the PCI ID (first 32 bits of PCI config space).
    uint32_t pci_id = communicator_->pci_config_read32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = communicator_->pci_config_read32(0, 0) >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");

    if ((libttsim_pci_device_id == TT_WORMHOLE_PCI_DEVICE_ID) ||
        (libttsim_pci_device_id == TT_BLACKHOLE_PCI_DEVICE_ID)) {
        // Compute physical address of BAR0 from PCI config registers.
        bar0_base = communicator_->pci_config_read32(0, 0x10);
        bar0_base |= uint64_t(communicator_->pci_config_read32(0, 0x14)) << 32;
        bar0_base &= ~15ull;  // ignore attributes, just obtain the physical address

        if (libttsim_pci_device_id == TT_WORMHOLE_PCI_DEVICE_ID) {
            tlb_region_size_ = 16 * 1024 * 1024;
        } else {
            tlb_region_size_ = 2 * 1024 * 1024;
        }
    }

    tlb_allocator_ = std::make_unique<TlbAllocator>(bar0_base, architecture_impl_.get());
    tlb_manager_ = std::make_unique<TTSimTLBManager>(this, tlb_allocator_.get(), communicator_.get());

    size_t default_tlb_size = tlb_allocator_->get_default_tlb_size();
    if (default_tlb_size > 0) {
        auto* sim_tlb_mgr = dynamic_cast<TTSimTLBManager*>(tlb_manager_.get());
        cached_tlb_window_ = sim_tlb_mgr->allocate_tlb_window({}, TlbMapping::WC, default_tlb_size);
    }
}

TTSimTTDevice::~TTSimTTDevice() = default;

void TTSimTTDevice::start_device() {}

void TTSimTTDevice::close_device() { communicator_->shutdown(); }

void TTSimTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (cached_tlb_window_) {
        cached_tlb_window_->write_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        communicator_->tile_write_bytes(core.x, core.y, addr, mem_ptr, size);
    }
}

void TTSimTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (cached_tlb_window_) {
        cached_tlb_window_->read_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        communicator_->tile_read_bytes(core.x, core.y, addr, mem_ptr, size);
    }
    communicator_->advance_clock(10);
}

void TTSimTTDevice::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if ((libttsim_pci_device_id == TT_WORMHOLE_PCI_DEVICE_ID) ||
        (libttsim_pci_device_id == TT_BLACKHOLE_PCI_DEVICE_ID)) {
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint32_t reset_value = uint32_t(soft_resets);
        write_to_device(&reset_value, translated_core, soft_reset_addr, sizeof(reset_value));
    } else if (libttsim_pci_device_id == TT_GRENDEL_PCI_DEVICE_ID) {
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

void TTSimTTDevice::dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size) {
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

void TTSimTTDevice::initialize_sysmem_functions() {
    communicator_->set_pcie_dma_mem_callbacks(
        [this](uint64_t a, void* p, uint32_t s) { pci_dma_read_bytes(a, p, s); },
        [this](uint64_t a, const void* p, uint32_t s) { pci_dma_write_bytes(a, p, s); });
}

void TTSimTTDevice::pci_dma_read_bytes(uint64_t paddr, void* p, uint32_t size) {
    uint64_t channel = paddr / (1ULL << 30);
    uint64_t offset = paddr % (1ULL << 30);
    sysmem_manager_->read_from_sysmem(channel, p, offset, size);
}

void TTSimTTDevice::pci_dma_write_bytes(uint64_t paddr, const void* p, uint32_t size) {
    uint64_t channel = paddr / (1ULL << 30);
    uint64_t offset = paddr % (1ULL << 30);
    sysmem_manager_->write_to_sysmem(channel, p, offset, size);
}

void TTSimTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    throw std::runtime_error("DRAM retraining is not supported in TTSim device.");
}

TLBManager* TTSimTTDevice::get_tlb_manager() { return tlb_manager_.get(); }

}  // namespace tt::umd
