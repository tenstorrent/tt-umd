// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/pcie/tlb_window.hpp"

#include <utility>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

TlbWindow::TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config, IoSafety io_safety) :
    tlb_handle(std::move(handle)), io_safety_(io_safety) {
    UMD_THROW(error::RuntimeError, "TlbWindow::TlbWindow is not implemented yet");
}

TlbHandle& TlbWindow::handle_ref() const { return *tlb_handle; }

void TlbWindow::write_aligned(uint64_t offset, const void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "TlbWindow::write_aligned is not implemented yet");
}

void TlbWindow::read_aligned(uint64_t offset, void* data, size_t size) {
    UMD_THROW(error::RuntimeError, "TlbWindow::read_aligned is not implemented yet");
}

void TlbWindow::configure(const TargetIoWindowConfig& config) {
    UMD_THROW(error::RuntimeError, "TlbWindow::configure is not implemented yet");
}

void TlbWindow::configure(const TargetIoWindowConfig& config, IoOrdering ordering) {
    UMD_THROW(error::RuntimeError, "TlbWindow::configure is not implemented yet");
}

TargetIoWindowConfig TlbWindow::get_target_config() const {
    UMD_THROW(error::RuntimeError, "TlbWindow::get_target_config is not implemented yet");
}

IoOrdering TlbWindow::get_io_ordering() const {
    UMD_THROW(error::RuntimeError, "TlbWindow::get_io_ordering is not implemented yet");
}

HostMemoryCaching TlbWindow::get_memory_caching_type() const {
    UMD_THROW(error::RuntimeError, "TlbWindow::get_memory_caching_type is not implemented yet");
}

size_t TlbWindow::get_size() const { UMD_THROW(error::RuntimeError, "TlbWindow::get_size is not implemented yet"); }

void TlbWindow::configure(const tlb_data& new_config) {
    UMD_THROW(error::RuntimeError, "TlbWindow::configure is not implemented yet");
}

uint64_t TlbWindow::get_base_address() const {
    UMD_THROW(error::RuntimeError, "TlbWindow::get_base_address is not implemented yet");
}

void TlbWindow::validate(uint64_t offset, size_t size) const {
    UMD_THROW(error::RuntimeError, "TlbWindow::validate is not implemented yet");
}

uint64_t TlbWindow::get_total_offset(uint64_t offset) const {
    UMD_THROW(error::RuntimeError, "TlbWindow::get_total_offset is not implemented yet");
}

tlb_data TlbWindow::make_tlb_config(
    uint64_t addr,
    tt_xy_pair core_end,
    NocId noc_id,
    uint64_t ordering,
    WindowFlags flags,
    bool mcast,
    tt_xy_pair core_start) const {
    UMD_THROW(error::RuntimeError, "TlbWindow::make_tlb_config is not implemented yet");
}

}  // namespace tt::umd
