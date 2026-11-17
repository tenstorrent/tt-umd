// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/tlb_window.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "common/unsupported.hpp"

namespace tt::umd {

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    tlb_handle(std::move(handle)), io_safety_(io_safety) {
    UMD_NOT_IMPLEMENTED();
}

void TlbWindow::write_aligned(uint64_t offset, const void* data, size_t size) { UMD_NOT_IMPLEMENTED(); }

void TlbWindow::read_aligned(uint64_t offset, void* data, size_t size) { UMD_NOT_IMPLEMENTED(); }

void TlbWindow::configure(const TargetIoWindowConfig& config) { UMD_NOT_IMPLEMENTED(); }

void TlbWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) { UMD_NOT_IMPLEMENTED(); }

TargetIoWindowConfig TlbWindow::get_target_config() const { UMD_NOT_IMPLEMENTED(); }

IoOrdering TlbWindow::get_io_ordering() const { UMD_NOT_IMPLEMENTED(); }

HostMemoryCaching TlbWindow::get_memory_caching_type() const { UMD_NOT_IMPLEMENTED(); }

TlbHandle& TlbWindow::handle_ref() const { UMD_NOT_IMPLEMENTED(); }

size_t TlbWindow::get_size() const { UMD_NOT_IMPLEMENTED(); }

void TlbWindow::configure(const tlb_data& new_config) { UMD_NOT_IMPLEMENTED(); }

uint64_t TlbWindow::get_base_address() const { UMD_NOT_IMPLEMENTED(); }

void TlbWindow::validate(uint64_t offset, size_t size) const { UMD_NOT_IMPLEMENTED(); }

uint64_t TlbWindow::get_total_offset(uint64_t offset) const { UMD_NOT_IMPLEMENTED(); }

tlb_data TlbWindow::make_tlb_config(
    uint64_t addr,
    tt_xy_pair core_end,
    NocId noc_id,
    uint64_t ordering,
    WindowFlags flags,
    bool mcast,
    tt_xy_pair core_start) const {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
