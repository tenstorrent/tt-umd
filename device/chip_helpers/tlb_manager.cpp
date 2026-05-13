// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/tlb_manager.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
    ZoneScopedC(tracy::Color::Cyan);
    UMD_ASSERT(
        ordering == tlb_data::Strict || ordering == tlb_data::Posted || ordering == tlb_data::Relaxed,
        error::RuntimeError,
        "Invalid ordering specified in Cluster::configure_tlb");
    log_debug(LogUMD, "Requesting TLB window of size {}", tlb_size);

    tlb_data config{};
    config.local_offset = address;
    config.x_end = core.x;
    config.y_end = core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = ordering;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    std::unique_ptr<TlbWindow> tlb_window = allocate_tlb_window(config, TlbMapping::WC, tlb_size);

    log_debug(
        LogUMD,
        "Configured TLB window for chip: {} core: {} size: {} address: {} ordering: {} tlb_id: {}",
        tt_device_->get_communication_device_id(),
        core.str(),
        tlb_size,
        address,
        ordering,
        tlb_window->handle_ref().get_tlb_id());

    // Use the actual allocated window size, not the caller-provided tlb_size, to
    // page-align the recorded base address. The caller may pass tlb_size = 0 to
    // mean "any size that fits"; in that case allocate_tlb_window picks one,
    // and dividing by the original argument here would be a divide-by-zero
    // (SIGFPE on Linux).
    const size_t window_size = tlb_window->handle_ref().get_size();
    tlb_config_map_.insert({tlb_window->handle_ref().get_tlb_id(), (address / window_size) * window_size});
    map_core_to_tlb_.insert({core, tlb_window->handle_ref().get_tlb_id()});
    tlb_windows_.insert({tlb_window->handle_ref().get_tlb_id(), std::move(tlb_window)});
}

TlbWindow* TLBManager::get_tlb_window(const tt_xy_pair core) {
    if (map_core_to_tlb_.find(core) != map_core_to_tlb_.end()) {
        return tlb_windows_.at(map_core_to_tlb_.at(core)).get();
    } else {
        UMD_THROW(error::RuntimeError, fmt::format("TLB window for core ({}, {}) not found.", core.x, core.y));
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

tlb_configuration TLBManager::get_tlb_configuration(tt_xy_pair core) {
    UMD_ASSERT(is_tlb_mapped(core), error::RuntimeError, fmt::format("TLB not mapped for core: {}", core.str()));

    int tlb_index = map_core_to_tlb_.at(core);
    return tt_device_->get_architecture_implementation()->get_tlb_configuration(tlb_index);
}

std::unique_ptr<TlbWindow> TLBManager::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    ZoneScopedC(tracy::Color::Cyan);
    return tt_device_->get_io_window(config, mapping, tlb_size);
}

void TLBManager::clear_mapped_tlbs() {
    ZoneScopedC(tracy::Color::Cyan);
    log_debug(LogUMD, "Clearing all TLB mappings.");
    tlb_config_map_.clear();
    map_core_to_tlb_.clear();
    tlb_windows_.clear();
}

};  // namespace tt::umd
