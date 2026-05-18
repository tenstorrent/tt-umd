// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/tlb_manager.hpp"

#include <stdexcept>

#include "tt-umd/tt_io.hpp"

namespace tt::umd {

TLBManager::TLBManager(TTDevice* tt_device) : tt_device_(tt_device) {}

void TLBManager::configure_tlb(tt_xy_pair, size_t, uint64_t, uint64_t) {}

bool TLBManager::is_tlb_mapped(tt_xy_pair) { return false; }

bool TLBManager::is_tlb_mapped(tt_xy_pair, uint64_t, uint32_t) { return false; }

Writer TLBManager::get_static_tlb_writer(tt_xy_pair) {
    throw std::runtime_error("TLBManager::get_static_tlb_writer is not supported in the mock build");
}

tlb_configuration TLBManager::get_tlb_configuration(tt_xy_pair) { return tlb_configuration{}; }

TlbWindow* TLBManager::get_tlb_window(const tt_xy_pair) { return nullptr; }

std::unique_ptr<TlbWindow> TLBManager::allocate_tlb_window(tlb_data, const TlbMapping, const size_t) { return nullptr; }

void TLBManager::clear_mapped_tlbs() {}

}  // namespace tt::umd
