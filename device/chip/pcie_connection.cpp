/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/pcie_connection.hpp"

#include <unistd.h>

#include <assert.hpp>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/tt_device/tt_device.hpp"

extern bool umd_use_noc1;
static constexpr uint64_t BH_4GB_TLB_SIZE = 4ULL * 1024 * 1024 * 1024;

namespace tt::umd {

PCIeConnection::PCIeConnection(TTDevice* tt_device, int num_host_mem_channels) :
    tt_device_(tt_device),
    tlb_manager_(std::make_unique<TLBManager>(tt_device_)),
    sysmem_manager_(std::make_unique<SysmemManager>(tlb_manager_.get(), num_host_mem_channels)),
    remote_communication_(std::make_unique<RemoteCommunication>(tt_device_, sysmem_manager_.get())) {}

PCIeConnection::~PCIeConnection() {
    // don't know if this is necessary..
    remote_communication_.reset();
    sysmem_manager_.reset();
    tlb_manager_.reset();
}

// General Chip Connection interface
void PCIeConnection::write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) {
    tt_xy_pair translated_core = core;

    const uint8_t* buffer_addr = static_cast<const uint8_t*>(src);

    if (tlb_manager_->is_tlb_mapped(translated_core, l1_dest, size)) {
        tlb_configuration tlb_description = tlb_manager_->get_tlb_configuration(translated_core);
        if (tt_device_->get_pci_device()->bar4_wc != nullptr && tlb_description.size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  write to DRAM (BAR4 space), we add offset
            // to which we write so write_block knows it needs to target BAR4
            tt_device_->write_block(
                (tlb_description.tlb_offset + l1_dest % tlb_description.size) + BAR0_BH_SIZE, size, buffer_addr);
        } else {
            tt_device_->write_block(tlb_description.tlb_offset + l1_dest % tlb_description.size, size, buffer_addr);
        }
    } else {
        std::string fallback_tlb = "LARGE_WRITE_TLB";
        const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
        auto lock = acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());

        while (size > 0) {
            auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(
                tlb_index, translated_core, l1_dest, tlb_manager_->dynamic_tlb_ordering_modes_.at(fallback_tlb));
            uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
            tt_device_->write_block(mapped_address, transfer_size, buffer_addr);

            size -= transfer_size;
            l1_dest += transfer_size;
            buffer_addr += transfer_size;
        }
        log_trace(LogSiliconDriver, "Write done Dynamic TLB with pid={}", (long)getpid());
    }
}

void PCIeConnection::read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) {
    uint8_t* buffer_addr = static_cast<uint8_t*>(dest);

    tt_xy_pair translated_core = core;

    if (tlb_manager_->is_tlb_mapped(translated_core, l1_src, size)) {
        tlb_configuration tlb_description = tlb_manager_->get_tlb_configuration(translated_core);
        if (tt_device_->get_pci_device()->bar4_wc != nullptr && tlb_description.size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  read from DRAM (BAR4 space), we add offset
            // from which we read so read_block knows it needs to target BAR4
            tt_device_->read_block(
                (tlb_description.tlb_offset + l1_src % tlb_description.size) + BAR0_BH_SIZE, size, buffer_addr);
        } else {
            tt_device_->read_block(tlb_description.tlb_offset + l1_src % tlb_description.size, size, buffer_addr);
        }
        log_trace(
            LogSiliconDriver,
            "  read_block called with tlb_offset: {}, tlb_size: {}",
            tlb_description.tlb_offset,
            tlb_description.size);
    } else {
        std::string fallback_tlb = "LARGE_READ_TLB";
        const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
        auto lock = acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());
        log_trace(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);
        while (size > 0) {
            auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(
                tlb_index, translated_core, l1_src, tlb_manager_->dynamic_tlb_ordering_modes_.at(fallback_tlb));
            uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
            tt_device_->read_block(mapped_address, transfer_size, buffer_addr);

            size -= transfer_size;
            l1_src += transfer_size;
            buffer_addr += transfer_size;
        }
        log_trace(LogSiliconDriver, "Read done Dynamic TLB with pid={}", (long)getpid());
    }
}

void PCIeConnection::write_to_device_reg(tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size) {
    std::string fallback_tlb = "REG_TLB";
    const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
    auto lock = lock_manager_.acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());
    log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);

    auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(tlb_index, core, reg_dest, tlb_data::Strict);
    tt_device_->write_regs(mapped_address, size / sizeof(uint32_t), src);
}

void PCIeConnection::read_from_device_reg(tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size) {
    std::string fallback_tlb = "REG_TLB";
    const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
    auto lock = lock_manager_.acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());
    log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);

    auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(tlb_index, core, reg_src, tlb_data::Strict);
    tt_device_->read_regs(mapped_address, size / sizeof(uint32_t), dest);
}

void PCIeConnection::pre_initialization_hook() {
    if (tlb_manager_ != nullptr) {
        initialize_tlb_manager();
    }
}

void PCIeConnection::initialization_hook() {}

void PCIeConnection::post_initialization_hook() {
    if (tlb_manager_ != nullptr) {
        initialize_default_chip_mutexes();
    }
}

void PCIeConnection::verify_initialization() {}

void PCIeConnection::start_connection() {
    // TODO: acquire mutex should live in Chip class. Currently we don't have unique id for all chips.
    // The lock here should suffice since we have to open Local chip to have Remote chips initialized.
    // TODO: Enable this once all tt-metal tests are passing.
    // chip_started_lock_.emplace(acquire_mutex(MutexType::CHIP_IN_USE,
    // tt_device_->get_pci_device()->get_device_num()));

    check_pcie_device_initialized();
    sysmem_manager_->pin_or_map_sysmem_to_device();
    if (!tt_device_->get_pci_device()->is_mapping_buffer_to_noc_supported()) {
        // If this is supported by the newer KMD, UMD doesn't have to program the iatu.
        init_pcie_iatus();
    }
    // initialize_membars();
}

void PCIeConnection::stop_connection() {
    sysmem_manager_->unpin_or_unmap_sysmem();
    // chip_started_lock_.reset();
}

// Specific PCIe interface
SysmemManager* PCIeConnection::get_sysmem_manager() { return sysmem_manager_.get(); }

TLBManager* PCIeConnection::get_tlb_manager() { return tlb_manager_.get(); }

int PCIeConnection::get_num_host_channels() {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogSiliconDriver,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    return sysmem_manager_->get_num_host_mem_channels();
}

int PCIeConnection::get_host_channel_size(std::uint32_t channel) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogSiliconDriver,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    TT_ASSERT(channel < get_num_host_channels(), "Querying size for a host channel that does not exist.");
    hugepage_mapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
    TT_ASSERT(hugepage_map.mapping_size, "Host channel size can only be queried after the device has been started.");
    return hugepage_map.mapping_size;
}

void PCIeConnection::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogSiliconDriver,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    sysmem_manager_->write_to_sysmem(channel, src, sysmem_dest, size);
}

void PCIeConnection::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogSiliconDriver,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    sysmem_manager_->read_from_sysmem(channel, dest, sysmem_src, size);
}

void PCIeConnection::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    static const std::string tlb_name = "LARGE_WRITE_TLB";

    const uint8_t* buffer = static_cast<const uint8_t*>(src);

    auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(tlb_name);
    auto ordering = tlb_manager_->dynamic_tlb_ordering_modes_.at(tlb_name);
    PCIDevice* pci_device = tt_device_->get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    // tt_xy_pair translated_core = translate_chip_coord_to_translated(core);
    tt_xy_pair translated_core = core;

    auto lock = acquire_mutex(tlb_name, pci_device->get_device_num());
    while (size > 0) {
        auto [axi_address, tlb_size] = tt_device_->set_dynamic_tlb(tlb_index, translated_core, addr, ordering);

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        tt_device_->dma_h2d(axi_address, buffer, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;
    }
}

void PCIeConnection::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        TT_THROW(
            "DMA operations are not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    static const std::string tlb_name = "LARGE_READ_TLB";
    uint8_t* buffer = static_cast<uint8_t*>(dst);
    auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(tlb_name);
    auto ordering = tlb_manager_->dynamic_tlb_ordering_modes_.at(tlb_name);
    PCIDevice* pci_device = tt_device_->get_pci_device().get();
    size_t dmabuf_size = pci_device->get_dma_buffer().size;

    // tt_xy_pair translated_core = translate_chip_coord_to_translated(core);
    tt_xy_pair translated_core = core;

    auto lock = acquire_mutex(tlb_name, pci_device->get_device_num());
    while (size > 0) {
        auto [axi_address, tlb_size] = tt_device_->set_dynamic_tlb(tlb_index, translated_core, addr, ordering);

        size_t transfer_size = std::min({size, tlb_size, dmabuf_size});

        tt_device_->dma_d2h(buffer, axi_address, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer += transfer_size;
    }
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> PCIeConnection::get_fast_pcie_static_tlb_write_callable() {
    const auto callable = [this](uint32_t byte_addr, uint32_t num_bytes, const uint8_t* buffer_addr) {
        tt_device_->write_block(byte_addr, num_bytes, buffer_addr);
    };
    return callable;
}

int PCIeConnection::get_numa_node() { return tt_device_->get_pci_device()->get_numa_node(); }

// Specific PCIe internals
void PCIeConnection::initialize_tlb_manager() {
    // Setup default dynamic tlbs.
    tlb_manager_->set_dynamic_tlb_config(
        "LARGE_READ_TLB", tt_device_->get_architecture_implementation()->get_mem_large_read_tlb());
    tlb_manager_->set_dynamic_tlb_config(
        "LARGE_WRITE_TLB", tt_device_->get_architecture_implementation()->get_mem_large_write_tlb());
    tlb_manager_->set_dynamic_tlb_config("REG_TLB", tt_device_->get_architecture_implementation()->get_reg_tlb());
    tlb_manager_->set_dynamic_tlb_config(
        "SMALL_READ_WRITE_TLB", tt_device_->get_architecture_implementation()->get_small_read_write_tlb());
}

void PCIeConnection::initialize_default_chip_mutexes() {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of
    // time here (during device init) since it's unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is
    // responsible for initializing the driver with this field set to cleanup after an aborted process.
    int pci_device_id = tt_device_->get_pci_device()->get_device_num();
    // Initialize Dynamic TLB mutexes
    for (auto& tlb : tlb_manager_->dynamic_tlb_config_) {
        lock_manager_.initialize_mutex(tlb.first, pci_device_id);
    }

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        lock_manager_.initialize_mutex(MutexType::REMOTE_ARC_MSG, pci_device_id);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic
    lock_manager_.initialize_mutex(MutexType::MEM_BARRIER, pci_device_id);

    // Initialize mutex guarding initialized chips.
    lock_manager_.initialize_mutex(MutexType::CHIP_IN_USE, pci_device_id);
}

void PCIeConnection::check_pcie_device_initialized() {
    if (test_setup_interface()) {
        throw std::runtime_error(
            "Device is incorrectly initialized. If this is a harvested Wormhole machine, it is likely that NOC "
            "Translation Tables are not enabled on device. These need to be enabled for the silicon driver to run.");
    }
}

int PCIeConnection::test_setup_interface() {
    // tt_xy_pair logical_core = translate_chip_coord_to_translated(CoreCoord(0, 0, CoreType::TENSIX,
    // CoordSystem::LOGICAL));
    tt_xy_pair logical_core;

    int ret_val = 0;
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        uint32_t mapped_reg =
            tt_device_
                ->set_dynamic_tlb(
                    tt_device_->get_architecture_implementation()->get_reg_tlb(), logical_core, 0xffb20108)
                .bar_offset;

        uint32_t regval = 0;
        tt_device_->read_regs(mapped_reg, 1, &regval);
        ret_val = (regval != HANG_READ_VALUE && (regval == 33)) ? 0 : 1;
        return ret_val;
    } else if (tt_device_->get_arch() == tt::ARCH::BLACKHOLE) {
        // TODO #768 figure out BH implementation
        return 0;
    } else {
        throw std::runtime_error(fmt::format("Unsupported architecture: {}", arch_to_str(tt_device_->get_arch())));
    }
}

void PCIeConnection::init_pcie_iatus() {
    // TODO: this should go away soon; KMD knows how to do this at page pinning time.
    for (size_t channel = 0; channel < sysmem_manager_->get_num_host_mem_channels(); channel++) {
        hugepage_mapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
        size_t region_size = hugepage_map.mapping_size;

        if (!hugepage_map.mapping) {
            throw std::runtime_error(fmt::format("Hugepages are not allocated for ch: {}", channel));
        }

        if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
            // TODO: stop doing this.  The intent was good, but it's not
            // documented and nothing takes advantage of it.
            if (channel == 3) {
                region_size = HUGEPAGE_CHANNEL_3_SIZE_LIMIT;
            }
        }
        tt_device_->configure_iatu_region(channel, hugepage_map.physical_address, region_size);
    }
}

void PCIeConnection::ethernet_broadcast_write(
    const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header) {
    // Depending on the device type, the implementation may vary.
    // Currently JTAG doesn't support remote communication.
    if (!remote_communication_) {
        TT_THROW(
            "Ethernet remote transfer is currently not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    // target_chip and target_core are ignored when broadcast is enabled.
    remote_communication_->write_to_non_mmio({0, 0, 0, 0}, {0, 0}, src, core_dest, size, true, broadcast_header);
}

void PCIeConnection::set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>& cores) {
    // Depending on the device type, the implementation may vary.
    // Currently JTAG doesn't support remote communication.
    if (!remote_communication_) {
        TT_THROW(
            "Ethernet remote transfer is currently not supported for {} devices.",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
    }

    // Set cores to be used by the broadcast communication.
    remote_communication_->set_remote_transfer_ethernet_cores(cores);
}

std::unique_lock<RobustMutex> PCIeConnection::acquire_mutex(std::string mutex_name, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_name, pci_device_id);
}

std::unique_lock<RobustMutex> PCIeConnection::acquire_mutex(MutexType mutex_type, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_type, pci_device_id);
}

}  // namespace tt::umd
