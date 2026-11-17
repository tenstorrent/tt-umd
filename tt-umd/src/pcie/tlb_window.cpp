// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/tlb_window.hpp"

#include <fmt/format.h>

#include <type_traits>
#include <utility>

#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    tlb_handle(std::move(handle)), io_safety_(io_safety) {
    // A freshly allocated window keeps whatever translation it last had, so it is always configured.
    TlbWindow::configure(config);
}

TlbHandle& TlbWindow::handle_ref() const { return *tlb_handle; }

void TlbWindow::write_aligned(uint64_t offset, const void* data, size_t size) { write_register(offset, data, size); }

void TlbWindow::read_aligned(uint64_t offset, void* data, size_t size) { read_register(offset, data, size); }

void TlbWindow::configure(const TargetIoWindowConfig& config) { configure(config, IoOrdering::Strict); }

void TlbWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) {
    // The direction field only steers which virtual channel the mapping uses, so every TLB window
    // takes it; any other flag must be one this implementation supports.
    const WindowFlags unsupported = config.flags & ~WindowFlags::DirectionMask & ~supported_window_flags();
    if (unsupported != WindowFlags::None) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "TLB window does not support window flags {:#x}.",
                static_cast<std::underlying_type_t<WindowFlags>>(unsupported)));
    }

    const NocId noc_id = config.noc.value_or(get_selected_noc_id());
    const bool mcast = config.core_end.has_value();
    tlb_data new_config = make_tlb_config(
        config.addr,
        mcast ? *config.core_end : config.core_start,
        noc_id,
        static_cast<uint64_t>(ordering),
        config.flags,
        mcast,
        mcast ? config.core_start : tt_xy_pair{});
    configure(new_config);
    window_flags_ = config.flags;
}

TargetIoWindowConfig TlbWindow::get_target_config() const {
    const tlb_data& config = tlb_handle->get_config();
    TargetIoWindowConfig target{};
    if (config.mcast != 0) {
        target.core_start = tt_xy_pair(config.x_start, config.y_start);
        target.core_end = tt_xy_pair(config.x_end, config.y_end);
    } else {
        target.core_start = tt_xy_pair(config.x_end, config.y_end);
    }
    target.addr = get_base_address();
    target.noc = static_cast<NocId>(config.noc_sel);
    target.flags = window_flags_;
    return target;
}

IoOrdering TlbWindow::get_io_ordering() const { return static_cast<IoOrdering>(tlb_handle->get_config().ordering); }

HostMemoryCaching TlbWindow::get_memory_caching_type() const {
    return tlb_handle->get_tlb_mapping() == TlbMapping::WC ? HostMemoryCaching::WC : HostMemoryCaching::UC;
}

size_t TlbWindow::get_size() const { return tlb_handle->get_size() - offset_from_aligned_addr; }

void TlbWindow::configure(const tlb_data& new_config) {
    // The hardware maps the window onto a size-aligned base; the remainder of the requested address
    // becomes an offset into the window.
    const uint64_t window_size = tlb_handle->get_size();
    tlb_data aligned_config = new_config;
    aligned_config.local_offset = new_config.local_offset & ~(window_size - 1);
    tlb_handle->configure(aligned_config);
    offset_from_aligned_addr = new_config.local_offset & (window_size - 1);
    window_flags_ = WindowFlags::None;
}

uint64_t TlbWindow::get_base_address() const {
    return tlb_handle->get_config().local_offset + offset_from_aligned_addr;
}

void TlbWindow::validate(uint64_t offset, size_t size) const {
    const size_t window_size = get_size();
    if (offset > window_size || size > window_size - offset) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Access of {} bytes at offset {:#x} is outside the {:#x}-byte TLB window.", size, offset, window_size));
    }
}

uint64_t TlbWindow::get_total_offset(uint64_t offset) const { return offset + offset_from_aligned_addr; }

tlb_data TlbWindow::make_tlb_config(
    uint64_t addr,
    tt_xy_pair core_end,
    NocId noc_id,
    uint64_t ordering,
    WindowFlags flags,
    bool mcast,
    tt_xy_pair core_start) const {
    tlb_data config{};
    config.local_offset = addr;
    config.x_end = core_end.x;
    config.y_end = core_end.y;
    config.x_start = core_start.x;
    config.y_start = core_start.y;
    config.noc_sel = static_cast<uint64_t>(noc_id);
    config.mcast = mcast ? 1 : 0;
    config.ordering = ordering;
    config.set_static_vc(get_architecture_tlbs(tlb_handle->get_arch()).get_static_vc(flags));
    return config;
}

}  // namespace tt::umd
