/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip_helpers/tlb_manager.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_io.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
    clear_tlb_mapping(core);
    TT_ASSERT(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        "Invalid ordering specified in Cluster::configure_tlb");
    log_debug(
        LogUMD,
        "Configuring TLB for chip: {} core: {} size: {} address: {} ordering: {}",
        tt_device_->get_pci_device()->get_device_num(),
        core.str(),
        tlb_size,
        address,
        ordering);

    tlb_data config{};
    config.local_offset = address;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = umd_use_noc1 ? 1 : 0;
    config.ordering = ordering;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    std::unique_ptr<TlbWindow> tlb_window = allocate_tlb_window(config, TlbMapping::WC, tlb_size);

    tlb_config_map_.insert({tlb_window->handle_ref().get_tlb_id(), (address / tlb_size) * tlb_size});
    map_core_to_tlb_.insert({core, tlb_window->handle_ref().get_tlb_id()});
    tlb_windows_.insert({tlb_window->handle_ref().get_tlb_id(), std::move(tlb_window)});
}

void TLBManager::clear_tlb_mapping(tt_xy_pair core) {
    if (is_tlb_mapped(core)) {
        log_debug(LogUMD, "Clearing TLB mapping for core: {}", core.str());
        auto tlb_id = map_core_to_tlb_.at(core);
        tlb_config_map_.erase(tlb_id);
        map_core_to_tlb_.erase(core);
        tlb_windows_.erase(tlb_id);
    }
}

TlbWindow* TLBManager::get_tlb_window(const tt_xy_pair core) {
    if (map_core_to_tlb_.find(core) != map_core_to_tlb_.end()) {
        return tlb_windows_.at(map_core_to_tlb_.at(core)).get();
    } else {
        throw std::runtime_error(fmt::format("TLB window for core ({}, {}) not found.", core.x, core.y));
    }
}

bool TLBManager::is_tlb_mapped(tt_xy_pair core) { return map_core_to_tlb_.find(core) != map_core_to_tlb_.end(); }

bool TLBManager::is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes) {
    if (!is_tlb_mapped(core)) {
        return false;
    }

    TlbWindow* tlb_window = get_tlb_window(core);

    return tlb_window->get_base_address() <= address &&
           address + size_in_bytes <= tlb_window->get_base_address() + tlb_window->get_size();
}

Writer TLBManager::get_static_tlb_writer(tt_xy_pair core) {
    if (!is_tlb_mapped(core)) {
        throw std::runtime_error(fmt::format("TLBs not initialized for core: {}", core.str()));
    }

    auto tlb_index = map_core_to_tlb_.at(core);
    auto tlb_data = tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);
    TlbWindow* tlb_window = tlb_windows_.at(tlb_index).get();

    return Writer(tlb_window->handle_ref().get_base(), tlb_data.size);
}

tlb_configuration TLBManager::get_tlb_configuration(tt_xy_pair core) {
    TT_ASSERT(is_tlb_mapped(core), "TLB not mapped for core: {}", core.str());

    int tlb_index = map_core_to_tlb_.at(core);
    return tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);
}

const std::vector<size_t> TLBManager::get_tlb_arch_sizes(const tt::ARCH arch) {
    constexpr uint32_t one_mb = 1 << 20;
    constexpr size_t one_gb = 1024ULL * one_mb;
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return {1 * one_mb, 2 * one_mb, 16 * one_mb};
        case tt::ARCH::BLACKHOLE:
            return {2 * one_mb, 4ULL * one_gb};
        default:
            throw std::runtime_error(fmt::format("Unsupported architecture: {}", static_cast<int>(arch)));
    }
}

std::unique_ptr<TlbWindow> TLBManager::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    if (tlb_size != 0) {
        return std::make_unique<TlbWindow>(tt_device_->get_pci_device()->allocate_tlb(tlb_size, mapping), config);
    }

    const std::vector<size_t> possible_arch_sizes = TLBManager::get_tlb_arch_sizes(tt_device_->get_arch());

    for (const auto& size : possible_arch_sizes) {
        std::unique_ptr<TlbWindow> tlb_window = nullptr;
        try {
            tlb_window = std::make_unique<TlbWindow>(tt_device_->get_pci_device()->allocate_tlb(size, mapping), config);
            return tlb_window;
        } catch (const std::exception& e) {
            log_error(LogUMD, "Failed to allocate TLB window of size {}: {}", size, e.what());
        }
    }

    throw std::runtime_error(fmt::format("Failed to allocate TLB window."));
}

void TLBManager::map_default_static_tlbs(SocDescriptor& soc_descriptor) {
    log_info(LogUMD, "Mapping default static TLBs.");

    tt::ARCH arch = tt_device_->get_arch();
    uint32_t static_tlb_size = tt_device_->get_architecture_implementation()->get_static_tlb_size();

    std::int32_t address = 0;
    // Setup static TLBs for all worker cores.
    for (const CoreCoord& core : soc_descriptor.get_cores(tt::CoreType::TENSIX, tt::CoordSystem::TRANSLATED)) {
        // Note: see issue #10107 in tt-metal
        // Strict is less performant than Posted, however, metal doesn't presently
        // use this on a perf path and the launch_msg "kernel config" needs to
        // arrive prior to the "go" message during device init and slow dispatch
        // Revisit this when we have a more flexible UMD api.
        tt_xy_pair translated_core(core.x, core.y);
        configure_tlb(translated_core, static_tlb_size, address, tlb_data::Strict);
    }
    // Setup static TLBs for all eth cores.
    for (const CoreCoord& core : soc_descriptor.get_cores(tt::CoreType::ETH, tt::CoordSystem::TRANSLATED)) {
        tt_xy_pair translated_core(core.x, core.y);
        configure_tlb(translated_core, static_tlb_size, address, tlb_data::Strict);
    }

    if (arch == tt::ARCH::BLACKHOLE) {
        // Setup static 4GB tlbs for DRAM cores.
        // Get the last port of each DRAM channel for configuring 4GB TLB.
        constexpr uint64_t four_gb = 4ULL * (1ULL << 30);

        for (auto dram_cores_for_channel : soc_descriptor.get_dram_cores()) {
            auto dram_core = dram_cores_for_channel[-1];  // Last core in the channel
            tt_xy_pair translated_core(dram_core.x, dram_core.y);
            configure_tlb(translated_core, four_gb, address, tlb_data::Strict);
        }
    }
}

void TLBManager::clear_mapped_tlbs() {
    log_info(LogUMD, "Clearing all TLB mappings.");
    // Clear all TLB mappings.
    tlb_config_map_.clear();
    map_core_to_tlb_.clear();
    tlb_windows_.clear();
}

};  // namespace tt::umd
