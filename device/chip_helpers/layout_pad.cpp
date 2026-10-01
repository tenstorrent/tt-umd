// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// EXPERIMENT (tt-metal#58148 split B-prime): a renamed, unreferenced copy of TLBManager.
// Nothing constructs or calls LayoutPad. It exists only so libtt-umd contains a translation unit of the same
// size, at the same place in the link order, as the TLBManager one that #3559 restores. If this alone keeps
// Blackhole healthy, the effect of #3559 is code layout, not behavior.

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/architecture_tlbs.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

class TTDevice;

class LayoutPad {
public:
    LayoutPad(TTDevice* tt_device);
    virtual ~LayoutPad() = default;

    // All tt_xy_pairs should be in TRANSLATED coords.
    void configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering);
    bool is_tlb_mapped(tt_xy_pair core);
    bool is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes);

    tlb_configuration get_tlb_configuration(tt_xy_pair core);

    // TODO: the following members will be moved to private once enough stuff is moved out of cluster.
    std::unordered_map<int32_t, uint64_t> tlb_config_map_;
    std::unordered_map<tt_xy_pair, std::int32_t> map_core_to_tlb_;
    std::unordered_map<int32_t, std::unique_ptr<TlbWindow>> tlb_windows_;

    TTDevice* get_tt_device() { return tt_device_; }

    TlbWindow* get_tlb_window(const tt_xy_pair core);

    virtual std::unique_ptr<TlbWindow> allocate_tlb_window(
        tlb_data config, const TlbMapping mapping = TlbMapping::WC, const size_t tlb_size = 0);

    // Clear all static TLB mappings.
    void clear_mapped_tlbs();

private:
    TTDevice* tt_device_;
};

}  // namespace tt::umd

namespace tt::umd {

LayoutPad::LayoutPad(TTDevice* tt_device) : tt_device_(tt_device) {}

void LayoutPad::configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering) {
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
    config.static_vc = get_architecture_tlbs(get_tt_device()->get_arch()).use_static_vc;
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

TlbWindow* LayoutPad::get_tlb_window(const tt_xy_pair core) {
    if (map_core_to_tlb_.find(core) != map_core_to_tlb_.end()) {
        return tlb_windows_.at(map_core_to_tlb_.at(core)).get();
    } else {
        UMD_THROW(error::RuntimeError, fmt::format("TLB window for core ({}, {}) not found.", core.x, core.y));
    }
}

bool LayoutPad::is_tlb_mapped(tt_xy_pair core) { return map_core_to_tlb_.find(core) != map_core_to_tlb_.end(); }

bool LayoutPad::is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes) {
    if (!is_tlb_mapped(core)) {
        return false;
    }

    TlbWindow* tlb_window = get_tlb_window(core);

    return tlb_window->get_base_address() <= address &&
           address + size_in_bytes <= tlb_window->get_base_address() + tlb_window->get_size();
}

tlb_configuration LayoutPad::get_tlb_configuration(tt_xy_pair core) {
    UMD_ASSERT(is_tlb_mapped(core), error::RuntimeError, fmt::format("TLB not mapped for core: {}", core.str()));

    int tlb_index = map_core_to_tlb_.at(core);
    return get_architecture_tlbs(tt_device_->get_arch()).get_configuration(tlb_index);
}

std::unique_ptr<TlbWindow> LayoutPad::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    ZoneScopedC(tracy::Color::Cyan);
    std::unique_ptr<TlbWindow> tlb_window = tt_device_->get_io_window(config, mapping, tlb_size);
    // Statically-mapped windows do device memory I/O, so they carry the same per-op MMIO timeout hang
    // check as the cached windows (see SiliconTlbWindow). Set once at creation rather than per op, so the
    // shared window's state is not mutated on the I/O path.
    TTDevice* tt_device = tt_device_;
    tlb_window->set_io_timeout_hang_check(
        [tt_device](NocId noc) -> bool { return tt_device->is_noc_hung(noc, TTDevice::HangAction::RETURN); });
    return tlb_window;
}

void LayoutPad::clear_mapped_tlbs() {
    ZoneScopedC(tracy::Color::Cyan);
    log_debug(LogUMD, "Clearing all TLB mappings.");
    tlb_config_map_.clear();
    map_core_to_tlb_.clear();
    tlb_windows_.clear();
}

};  // namespace tt::umd
