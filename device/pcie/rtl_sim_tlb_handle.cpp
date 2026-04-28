// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/pcie/simulation_tlb_provider.hpp"

namespace tt::umd {

RtlSimTlbHandle::RtlSimTlbHandle(SimulationTlbProvider* manager, int tlb_id, size_t size, TlbMapping mapping) :
    manager_(manager) {
    tlb_id_ = tlb_id;
    tlb_size_ = size;
    tlb_mapping_ = mapping;

    if (manager_) {
        // This is a fake, non-dereferenceable pointer used only for address arithmetic.
        // For RTL sim, bar0_base is 0, so this will be a near-null address.
        tlb_base_ = reinterpret_cast<uint8_t*>(manager_->get_tlb_address_from_index(tlb_id_));
    }

    log_debug(
        LogUMD,
        "Created RtlSimTlbHandle with ID {} size {} address 0x{:x}",
        tlb_id_,
        tlb_size_,
        reinterpret_cast<uint64_t>(tlb_base_));
}

std::unique_ptr<RtlSimTlbHandle> RtlSimTlbHandle::create(
    SimulationTlbProvider* manager, int tlb_id, size_t size, TlbMapping mapping) {
    return std::unique_ptr<RtlSimTlbHandle>(new RtlSimTlbHandle(manager, tlb_id, size, mapping));
}

RtlSimTlbHandle::~RtlSimTlbHandle() noexcept { RtlSimTlbHandle::free_tlb(); }

void RtlSimTlbHandle::configure(const tlb_data& new_config) {
    tlb_config_ = new_config;
    tlb_config_.local_offset = new_config.local_offset / tlb_size_;

    // These fields are not used for RTL sim.
    tlb_config_.ordering = 0;
    tlb_config_.static_vc = 0;

    log_debug(
        LogUMD,
        "Configured RTL sim TLB {} with local_offset: {}, x_end: {}, y_end: {}",
        tlb_id_,
        tlb_config_.local_offset,
        tlb_config_.x_end,
        tlb_config_.y_end);
}

void RtlSimTlbHandle::free_tlb() noexcept {
    if (manager_) {
        manager_->deallocate_tlb_index(tlb_id_);
        manager_ = nullptr;

        log_debug(LogUMD, "Freed RTL sim TLB with ID {}", tlb_id_);
    }
}

tt::ARCH RtlSimTlbHandle::get_arch() const { return manager_->get_arch(); }

}  // namespace tt::umd
