// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/local_chip.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

static_assert(!std::is_abstract<LocalChip>(), "LocalChip must be non-abstract.");

// TLB size for DRAM on blackhole - 4GB.
const uint64_t BH_4GB_TLB_SIZE = 4ULL * 1024 * 1024 * 1024;

std::unique_ptr<LocalChip> LocalChip::create(
    int physical_device_id, std::string sdesc_path, int num_host_mem_channels, IODeviceType device_type) {
    // Create TTDevice and make sure the arc is ready so we can read its telemetry.
    auto tt_device = TTDevice::create(physical_device_id, device_type);
    tt_device->init_tt_device();

    SocDescriptor soc_descriptor;
    if (sdesc_path.empty()) {
        // In case soc descriptor yaml wasn't passed, we create soc descriptor with default values for the architecture.
        soc_descriptor = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
    } else {
        soc_descriptor = SocDescriptor(sdesc_path, tt_device->get_chip_info());
    }

    std::unique_ptr<TLBManager> tlb_manager = nullptr;
    std::unique_ptr<SysmemManager> sysmem_manager = nullptr;
    std::unique_ptr<RemoteCommunication> remote_communication = nullptr;

    // The variables bellow are only needed when using PCIe.
    // JTAG(currently the only communication protocol other than PCIe) has no use of them.
    if (device_type == IODeviceType::PCIe) {
        tlb_manager = std::make_unique<TLBManager>(tt_device.get());
        sysmem_manager = std::make_unique<SysmemManager>(tlb_manager.get(), num_host_mem_channels);
    }
    // Note that the eth_coord is not important here since this is only used for eth broadcasting.
    remote_communication = RemoteCommunication::create_remote_communication(
        tt_device.get(),
        {0, 0, 0, 0},
        sysmem_manager->get_num_host_mem_channels() > 0 ? sysmem_manager.get() : nullptr);

    return std::unique_ptr<LocalChip>(new LocalChip(
        soc_descriptor,
        std::move(tt_device),
        std::move(tlb_manager),
        std::move(sysmem_manager),
        std::move(remote_communication),
        num_host_mem_channels));
}

std::unique_ptr<LocalChip> LocalChip::create(
    int physical_device_id, SocDescriptor soc_descriptor, int num_host_mem_channels, IODeviceType device_type) {
    // Create TTDevice and make sure the arc is ready so we can read its telemetry.
    // physical_device_id is not actually physical for JTAG devices here.
    // It represents the index within a vector of jlink devices discovered by JtagDevice.
    auto tt_device = TTDevice::create(physical_device_id, device_type);
    tt_device->init_tt_device();

    std::unique_ptr<TLBManager> tlb_manager = nullptr;
    std::unique_ptr<SysmemManager> sysmem_manager = nullptr;
    std::unique_ptr<RemoteCommunication> remote_communication = nullptr;

    // The variables bellow are only needed when using PCIe.
    // JTAG(currently the only communication protocol other than PCIe) has no use of them.
    if (device_type == IODeviceType::PCIe) {
        tlb_manager = std::make_unique<TLBManager>(tt_device.get());
        sysmem_manager = std::make_unique<SysmemManager>(tlb_manager.get(), num_host_mem_channels);
    }
    // Note that the eth_coord is not important here since this is only used for eth broadcasting.
    remote_communication = RemoteCommunication::create_remote_communication(
        tt_device.get(),
        {0, 0, 0, 0},
        sysmem_manager->get_num_host_mem_channels() > 0 ? sysmem_manager.get() : nullptr);

    return std::unique_ptr<LocalChip>(new LocalChip(
        soc_descriptor,
        std::move(tt_device),
        std::move(tlb_manager),
        std::move(sysmem_manager),
        std::move(remote_communication),
        num_host_mem_channels));
}

LocalChip::LocalChip(
    SocDescriptor soc_descriptor,
    std::unique_ptr<TTDevice> tt_device,
    std::unique_ptr<TLBManager> tlb_manager,
    std::unique_ptr<SysmemManager> sysmem_manager,
    std::unique_ptr<RemoteCommunication> remote_communication,
    int num_host_mem_channels) :
    Chip(tt_device->get_chip_info(), soc_descriptor),
    tlb_manager_(std::move(tlb_manager)),
    sysmem_manager_(std::move(sysmem_manager)),
    remote_communication_(std::move(remote_communication)),
    tt_device_(std::move(tt_device)) {
    wait_chip_to_be_ready();
    if (tlb_manager_ != nullptr) {
        initialize_default_chip_mutexes();
    }
}

LocalChip::~LocalChip() {
    // Deconstruct the LocalChip in the right order.
    // TODO: Use intializers in constructor to avoid having to explicitly declare the order of destruction.
    cached_pcie_dma_tlb_window.reset();
    cached_wc_tlb_window.reset();
    cached_uc_tlb_window.reset();
    remote_communication_.reset();
    sysmem_manager_.reset();
    tlb_manager_.reset();
    tt_device_.reset();
}

void LocalChip::initialize_default_chip_mutexes() {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of
    // time here (during device init) since it's unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is
    // responsible for initializing the driver with this field set to cleanup after an aborted process.
    int pci_device_id = tt_device_->get_pci_device()->get_device_num();

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        lock_manager_.initialize_mutex(MutexType::REMOTE_ARC_MSG, pci_device_id);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic.
    lock_manager_.initialize_mutex(MutexType::MEM_BARRIER, pci_device_id);

    // Initialize mutex guarding initialized chips.
    lock_manager_.initialize_mutex(MutexType::CHIP_IN_USE, pci_device_id);
}

void LocalChip::initialize_membars() {
    set_membar_flag(
        soc_descriptor_.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED),
        MemBarFlag::RESET,
        l1_address_params.tensix_l1_barrier_base);
    set_membar_flag(
        soc_descriptor_.get_cores(CoreType::ETH, CoordSystem::TRANSLATED),
        MemBarFlag::RESET,
        l1_address_params.eth_l1_barrier_base);

    std::vector<CoreCoord> dram_cores_vector = {};
    for (std::uint32_t dram_idx = 0; dram_idx < soc_descriptor_.get_num_dram_channels(); dram_idx++) {
        dram_cores_vector.push_back(soc_descriptor_.get_dram_core_for_channel(dram_idx, 0, CoordSystem::TRANSLATED));
    }
    set_membar_flag(dram_cores_vector, MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE);
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* LocalChip::get_sysmem_manager() { return sysmem_manager_.get(); }

TLBManager* LocalChip::get_tlb_manager() { return tlb_manager_.get(); }

bool LocalChip::is_mmio_capable() const { return true; }

void LocalChip::start_device() {
    if (tt_device_->get_communication_device_type() == IODeviceType::JTAG) {
        return;
    }

    // TODO: acquire mutex should live in Chip class. Currently we don't have unique id for all chips.
    // The lock here should suffice since we have to open Local chip to have Remote chips initialized.
    chip_started_lock_.emplace(acquire_mutex(MutexType::CHIP_IN_USE, tt_device_->get_pci_device()->get_device_num()));

    check_pcie_device_initialized();
    sysmem_manager_->pin_or_map_sysmem_to_device();
    if (!tt_device_->get_pci_device()->is_mapping_buffer_to_noc_supported()) {
        // If this is supported by the newer KMD, UMD doesn't have to program the iatu.
        init_pcie_iatus();
    }
    initialize_membars();
}

void LocalChip::close_device() {
    // Investigating https://github.com/tenstorrent/tt-metal/issues/25377 found that closing device that was already put
    // in LONG_IDLE by tt-smi reset would hang
    if ((uint32_t)get_clock() != get_tt_device()->get_min_clock_freq()) {
        set_power_state(DevicePowerState::LONG_IDLE);
        assert_risc_reset(RiscType::ALL);
        // Unmapping might be needed even in the case chip was reset due to kmd mappings.
        if (sysmem_manager_) {
            sysmem_manager_->unpin_or_unmap_sysmem();
        }
    }
    chip_started_lock_.reset();
};

int LocalChip::get_num_host_channels() {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    return sysmem_manager_->get_num_host_mem_channels();
}

int LocalChip::get_host_channel_size(std::uint32_t channel) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    TT_ASSERT(channel < get_num_host_channels(), "Querying size for a host channel that does not exist.");
    HugepageMapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
    TT_ASSERT(hugepage_map.mapping_size, "Host channel size can only be queried after the device has been started.");
    return hugepage_map.mapping_size;
}

void LocalChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    sysmem_manager_->write_to_sysmem(channel, src, sysmem_dest, size);
}

void LocalChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    sysmem_manager_->read_from_sysmem(channel, dest, sysmem_src, size);
}

void LocalChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    log_trace(
        LogUMD,
        "Chip::write_to_device to {} dev {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_dest,
        size);

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->write_to_device(src, translated_core, l1_dest, size);
        return;
    }

    const uint8_t* buffer_addr = static_cast<const uint8_t*>(src);

    if (tlb_manager_->is_tlb_mapped(translated_core, l1_dest, size)) {
        TlbWindow* tlb_window = tlb_manager_->get_tlb_window(translated_core);
        tlb_window->write_block(l1_dest - tlb_window->get_base_address(), src, size);
    } else {
        std::lock_guard<std::mutex> lock(wc_tlb_lock);
        get_cached_wc_tlb_window()->write_block_reconfigure(src, translated_core, l1_dest, size, tlb_data::Relaxed);
    }
}

void LocalChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    log_trace(
        LogUMD,
        "Chip::read_from_device from {} device {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_src,
        size);

    uint8_t* buffer_addr = static_cast<uint8_t*>(dest);

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device(dest, translated_core, l1_src, size);
        return;
    }
    if (tlb_manager_->is_tlb_mapped(translated_core, l1_src, size)) {
        TlbWindow* tlb_window = tlb_manager_->get_tlb_window(translated_core);
        tlb_window->read_block(l1_src - tlb_window->get_base_address(), dest, size);
    } else {
        std::lock_guard<std::mutex> lock(wc_tlb_lock);
        get_cached_wc_tlb_window()->read_block_reconfigure(dest, translated_core, l1_src, size, tlb_data::Relaxed);
    }
}

void LocalChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    if (get_tt_device()->get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) write.",
            get_tt_device()->get_communication_device_id());
        write_to_device(core, src, addr, size);
        return;
    }

    std::lock_guard<std::mutex> lock(pcie_dma_lock);

    const uint8_t* buffer = static_cast<const uint8_t*>(src);
    PCIDevice* pci_device = tt_device_->get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base = get_tt_device()
                                ->get_architecture_implementation()
                                ->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id())
                                .tlb_offset;
    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    while (size > 0) {
        auto tlb_size = tlb_window->get_size();

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        tt_device_->dma_h2d(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void LocalChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    if (get_tt_device()->get_pci_device()->get_dma_buffer().buffer == nullptr) {
        log_warning(
            LogUMD,
            "DMA buffer was not allocated for PCI device {}, falling back to non-DMA (regular MMIO TLB) read.",
            get_tt_device()->get_communication_device_id());
        read_from_device(core, dst, addr, size);
        return;
    }

    std::lock_guard<std::mutex> lock(pcie_dma_lock);

    uint8_t* buffer = static_cast<uint8_t*>(dst);
    PCIDevice* pci_device = tt_device_->get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    tlb_data config{};
    config.local_offset = addr;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Relaxed;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_pcie_dma_tlb_window(config);

    auto axi_address_base = get_tt_device()
                                ->get_architecture_implementation()
                                ->get_tlb_configuration(tlb_window->handle_ref().get_tlb_id())
                                .tlb_offset;

    const size_t tlb_handle_size = tlb_window->handle_ref().get_size();
    auto axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));

    while (size > 0) {
        auto tlb_size = tlb_window->get_size();
        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        tt_device_->dma_d2h(buffer, axi_address, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;

        config.local_offset = addr;
        tlb_window->configure(config);
        axi_address = axi_address_base + (addr - (addr & ~(tlb_handle_size - 1)));
    }
}

void LocalChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Size must be a multiple of 4 bytes");
    }

    if (reg_dest % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Register address must be 4-byte aligned");
    }

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->write_to_device(src, core, reg_dest, size);
        return;
    }

    std::lock_guard<std::mutex> lock(uc_tlb_lock);

    auto translated_core = translate_chip_coord_to_translated(core);
    tlb_data config{};
    config.local_offset = reg_dest;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Strict;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_uc_tlb_window();
    tlb_window->configure(config);

    tlb_window->write_register(reg_dest - tlb_window->get_base_address(), src, size);
}

void LocalChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Size must be a multiple of 4 bytes");
    }

    if (reg_src % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Register address must be 4-byte aligned");
    }

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device(dest, core, reg_src, size);
        return;
    }

    std::lock_guard<std::mutex> lock(uc_tlb_lock);

    auto translated_core = translate_chip_coord_to_translated(core);
    tlb_data config{};
    config.local_offset = reg_src;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = tlb_data::Strict;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_uc_tlb_window();
    tlb_window->configure(config);

    tlb_window->read_register(reg_src - tlb_window->get_base_address(), dest, size);
}

void LocalChip::ethernet_broadcast_write(
    const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header) {
    // Depending on the device type, the implementation may vary.
    // Currently JTAG doesn't support remote communication.
    if (!remote_communication_) {
        TT_THROW(
            "Ethernet remote transfer is currently not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    // target_chip and target_core are ignored when broadcast is enabled.
    remote_communication_->write_to_non_mmio({0, 0}, src, core_dest, size, true, broadcast_header);
}

void LocalChip::wait_for_non_mmio_flush() {
    // This is a local chip, so no need to flush remote communication.
}

void LocalChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {
    // Set cores to be used by the broadcast communication.
    remote_communication_->set_remote_transfer_ethernet_cores(
        get_soc_descriptor().translate_coords_to_xy_pair(cores, CoordSystem::TRANSLATED));
}

void LocalChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {
    // Set cores to be used by the broadcast communication.
    remote_communication_->set_remote_transfer_ethernet_cores(
        get_soc_descriptor().get_eth_xy_pairs_for_channels(channels, CoordSystem::TRANSLATED));
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(std::string mutex_name, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_name, pci_device_id);
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(MutexType mutex_type, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_type, pci_device_id);
}

void LocalChip::check_pcie_device_initialized() {
    if (test_setup_interface()) {
        throw std::runtime_error(
            "Device is incorrectly initialized. If this is a harvested Wormhole machine, it is likely that NOC "
            "Translation Tables are not enabled on device. These need to be enabled for the silicon driver to run.");
    }
}

int LocalChip::test_setup_interface() {
    int ret_val = 0;
    if (soc_descriptor_.arch == tt::ARCH::WORMHOLE_B0) {
        uint32_t regval = 0;
        read_from_device_reg(CoreCoord(1, 0, CoreType::ETH, CoordSystem::NOC0), &regval, 0xffb20108, sizeof(uint32_t));
        ret_val = (regval != HANG_READ_VALUE && (regval == 33)) ? 0 : 1;
        return ret_val;
    } else if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
        // TODO #768 figure out BH implementation.
        return 0;
    } else {
        throw std::runtime_error(fmt::format("Unsupported architecture: {}", arch_to_str(soc_descriptor_.arch)));
    }
}

void LocalChip::init_pcie_iatus() {
    // TODO: this should go away soon; KMD knows how to do this at page pinning time.
    for (size_t channel = 0; channel < sysmem_manager_->get_num_host_mem_channels(); channel++) {
        HugepageMapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
        size_t region_size = hugepage_map.mapping_size;

        if (!hugepage_map.mapping) {
            throw std::runtime_error(fmt::format("Hugepages are not allocated for ch: {}", channel));
        }

        if (soc_descriptor_.arch == tt::ARCH::WORMHOLE_B0) {
            // TODO: stop doing this.  The intent was good, but it's not
            // documented and nothing takes advantage of it.
            if (channel == 3) {
                region_size = HUGEPAGE_CHANNEL_3_SIZE_LIMIT;
            }
        }
        tt_device_->configure_iatu_region(channel, hugepage_map.physical_address, region_size);
    }
}

void LocalChip::set_membar_flag(
    const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr) {
    tt_driver_atomics::sfence();  // Ensure that writes before this do not get reordered
    std::unordered_set<CoreCoord> cores_synced = {};
    std::vector<uint32_t> barrier_val_vec = {barrier_value};
    for (const auto& core : cores) {
        write_to_device(core, barrier_val_vec.data(), barrier_addr, barrier_val_vec.size() * sizeof(uint32_t));
    }
    tt_driver_atomics::sfence();  // Ensure that all writes in the Host WC buffer are flushed
    while (cores_synced.size() != cores.size()) {
        for (const auto& core : cores) {
            if (cores_synced.find(core) == cores_synced.end()) {
                uint32_t readback_val;
                read_from_device(core, &readback_val, barrier_addr, sizeof(std::uint32_t));
                if (readback_val == barrier_value) {
                    cores_synced.insert(core);
                } else {
                    log_trace(
                        LogUMD,
                        "Waiting for core {} to recieve mem bar flag {} in function",
                        core.str(),
                        barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned.
    tt_driver_atomics::mfence();
}

void LocalChip::insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr) {
    // Ensure that this memory barrier is atomic across processes/threads.
    auto lock = lock_manager_.acquire_mutex(MutexType::MEM_BARRIER, tt_device_->get_pci_device()->get_device_num());
    set_membar_flag(cores, MemBarFlag::SET, barrier_addr);
    set_membar_flag(cores, MemBarFlag::RESET, barrier_addr);
}

void LocalChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {
    if (cores.size()) {
        // Insert barrier on specific cores with L1.
        std::vector<CoreCoord> workers_to_sync = {};
        std::vector<CoreCoord> eth_to_sync = {};

        for (const auto& core : cores) {
            auto core_from_soc = soc_descriptor_.get_coord_at(core, core.coord_system);
            if (core_from_soc.core_type == CoreType::TENSIX) {
                workers_to_sync.push_back(core);
            } else if (core_from_soc.core_type == CoreType::ETH) {
                eth_to_sync.push_back(core);
            } else {
                TT_THROW("Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
            }
        }
        insert_host_to_device_barrier(workers_to_sync, l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(eth_to_sync, l1_address_params.eth_l1_barrier_base);
    } else {
        // Insert barrier on all cores with L1.
        insert_host_to_device_barrier(
            soc_descriptor_.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED),
            l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(
            soc_descriptor_.get_cores(CoreType::ETH, CoordSystem::TRANSLATED), l1_address_params.eth_l1_barrier_base);
    }
}

void LocalChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {
    if (cores.size()) {
        for (const auto& core : cores) {
            TT_ASSERT(
                soc_descriptor_.get_coord_at(core, core.coord_system).core_type == CoreType::DRAM,
                "Can only insert a DRAM Memory barrier on DRAM cores.");
        }
        std::vector<CoreCoord> dram_cores_vector = std::vector<CoreCoord>(cores.begin(), cores.end());
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    } else {
        // Insert Barrier on all DRAM Cores.
        std::vector<CoreCoord> dram_cores_vector = {};
        for (std::uint32_t dram_idx = 0; dram_idx < soc_descriptor_.get_num_dram_channels(); dram_idx++) {
            dram_cores_vector.push_back(
                soc_descriptor_.get_dram_core_for_channel(dram_idx, 0, CoordSystem::TRANSLATED));
        }
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    }
}

void LocalChip::dram_membar(const std::unordered_set<uint32_t>& channels) {
    std::unordered_set<CoreCoord> dram_cores_to_sync = {};
    for (const auto& chan : channels) {
        dram_cores_to_sync.insert(soc_descriptor_.get_dram_core_for_channel(chan, 0, CoordSystem::TRANSLATED));
    }
    dram_membar(dram_cores_to_sync);
}

void LocalChip::deassert_risc_resets() {
    if (soc_descriptor_.arch != tt::ARCH::BLACKHOLE) {
        arc_msg(
            wormhole::ARC_MSG_COMMON_PREFIX |
                tt_device_->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(),
            true,
            {0, 0});
    }
}

int LocalChip::get_clock() { return tt_device_->get_clock(); }

int LocalChip::get_numa_node() { return tt_device_->get_pci_device()->get_numa_node(); }

TlbWindow* LocalChip::get_cached_wc_tlb_window() {
    if (cached_wc_tlb_window == nullptr) {
        cached_wc_tlb_window = std::make_unique<TlbWindow>(get_tt_device()->get_pci_device()->allocate_tlb(
            get_tt_device()->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::WC));
        return cached_wc_tlb_window.get();
    }

    return cached_wc_tlb_window.get();
}

TlbWindow* LocalChip::get_cached_uc_tlb_window() {
    if (cached_uc_tlb_window == nullptr) {
        cached_uc_tlb_window = std::make_unique<TlbWindow>(get_tt_device()->get_pci_device()->allocate_tlb(
            get_tt_device()->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::UC));
        return cached_uc_tlb_window.get();
    }

    return cached_uc_tlb_window.get();
}

TlbWindow* LocalChip::get_cached_pcie_dma_tlb_window(tlb_data config) {
    if (cached_pcie_dma_tlb_window == nullptr) {
        cached_pcie_dma_tlb_window = std::make_unique<TlbWindow>(
            get_tt_device()->get_pci_device()->allocate_tlb(16 * 1024 * 1024, TlbMapping::WC), config);
        return cached_pcie_dma_tlb_window.get();
    }

    cached_pcie_dma_tlb_window->configure(config);
    return cached_pcie_dma_tlb_window.get();
}

void LocalChip::noc_multicast_write(void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    // TODO: Support other core types once needed.
    if (core_start.core_type != CoreType::TENSIX || core_end.core_type != CoreType::TENSIX) {
        TT_THROW("noc_multicast_write is only supported for Tensix cores.");
    }

    // Multicast write relies on PCIe-specific TLB operations; ensure the communication device is PCIe.
    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW("noc_multicast_write is only supported on PCIe devices.");
    }

    get_cached_wc_tlb_window()->noc_multicast_write_reconfigure(
        dst,
        size,
        translate_chip_coord_to_translated(core_start),
        translate_chip_coord_to_translated(core_end),
        addr,
        tlb_data::Relaxed);
}
}  // namespace tt::umd
