// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/tlb_manager.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_io.hpp"
#include "umd/device/types/tlb.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
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

void TLBManager::configure_tlb_kmd(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
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

};  // namespace tt::umd
