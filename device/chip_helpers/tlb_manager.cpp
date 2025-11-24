// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
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

static constexpr uint64_t DEFAULT_ORDERING_MODE = tlb_data::Relaxed;

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(tt_xy_pair core, int32_t tlb_index, uint64_t address, uint64_t ordering) {
    TT_ASSERT(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        "Invalid ordering specified in Cluster::configure_tlb");
    log_debug(
        LogUMD,
        "Configuring TLB for chip: {} core: {} tlb_index: {} address: {} ordering: {}",
        tt_device_->get_pci_device()->get_device_num(),
        core.str(),
        tlb_index,
        address,
        ordering);
    TT_ASSERT(tlb_config_map_.find(tlb_index) == tlb_config_map_.end(), "TLB index already configured {}", tlb_index);

    tt_device_->set_dynamic_tlb(tlb_index, core, address, ordering);
    auto tlb_size = tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index).size;
    tlb_config_map_.insert({tlb_index, (address / tlb_size) * tlb_size});
    map_core_to_tlb_.insert({core, tlb_index});
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
    config.static_vc = (get_tt_device()->get_arch() == tt::ARCH::BLACKHOLE) ? false : true;
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

void TLBManager::set_dynamic_tlb_config(std::string fallback_tlb_name, int32_t tlb_index) {
    TT_ASSERT(
        dynamic_tlb_config_.find(fallback_tlb_name) == dynamic_tlb_config_.end(),
        "Dynamic TLB already configured for {}",
        fallback_tlb_name);
    dynamic_tlb_config_.insert({fallback_tlb_name, tlb_index});
    dynamic_tlb_ordering_modes_[fallback_tlb_name] = DEFAULT_ORDERING_MODE;
}

void TLBManager::set_dynamic_tlb_config_ordering(std::string fallback_tlb_name, uint64_t ordering) {
    TT_ASSERT(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        "Invalid ordering specified in set_dynamic_tlb_config_ordering.");
    TT_ASSERT(
        fallback_tlb_name != "LARGE_READ_TLB" && fallback_tlb_name != "LARGE_WRITE_TLB",
        "Ordering modes for LARGE_READ_TLB and LARGE_WRITE_TLB cannot be modified.");
    TT_ASSERT(
        dynamic_tlb_config_.find(fallback_tlb_name) != dynamic_tlb_config_.end(),
        "Dynamic TLB not configured {}",
        fallback_tlb_name);

    dynamic_tlb_ordering_modes_[fallback_tlb_name] = ordering;
}

bool TLBManager::address_in_tlb_space(uint64_t address, uint32_t size_in_bytes, int32_t tlb_index, uint64_t tlb_size) {
    if (tlb_config_map_.find(tlb_index) != tlb_config_map_.end()) {
        auto mapped_address = tlb_config_map_.at(tlb_index);
        return address >= mapped_address && (address + size_in_bytes <= mapped_address + tlb_size);
    }
    return false;
}

bool TLBManager::is_tlb_mapped(tt_xy_pair core) { return map_core_to_tlb_.find(core) != map_core_to_tlb_.end(); }

bool TLBManager::is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes) {
    if (!is_tlb_mapped(core)) {
        return false;
    }

    int32_t tlb_index = map_core_to_tlb_.at(core);
    tlb_configuration tlb_description = tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);

    return address_in_tlb_space(address, size_in_bytes, tlb_index, tlb_description.size);
}

Writer TLBManager::get_static_tlb_writer(tt_xy_pair core) {
    if (!is_tlb_mapped(core)) {
        throw std::runtime_error(fmt::format("TLBs not initialized for core: {}", core.str()));
    }

    if (!tt_device_->get_pci_device()->bar0_wc) {
        throw std::runtime_error("No write-combined mapping for BAR0");
    }

    auto tlb_index = map_core_to_tlb_.at(core);
    auto tlb_data = tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);

    auto* base = reinterpret_cast<uint8_t*>(tt_device_->get_pci_device()->bar0_wc);

    return Writer(base + tlb_data.tlb_offset, tlb_data.size);
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
