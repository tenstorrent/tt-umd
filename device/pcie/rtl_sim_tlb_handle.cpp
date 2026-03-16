// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/rtl_sim_tlb_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip_helpers/simulation_tlb_manager.hpp"

namespace tt::umd {

RtlSimTlbHandle::RtlSimTlbHandle(SimulationTlbManager* manager, int tlb_id, size_t size, TlbMapping mapping) :
    manager_(manager), tlb_id_(tlb_id), size_(size), mapping_(mapping) {
    if (manager_) {
        address_ = manager_->get_tlb_address_from_index(tlb_id_);
    }

    log_debug(LogUMD, "Created RtlSimTlbHandle with ID {} size {} address 0x{:x}", tlb_id_, size_, address_);
}

std::unique_ptr<RtlSimTlbHandle> RtlSimTlbHandle::create(
    SimulationTlbManager* manager, int tlb_id, size_t size, TlbMapping mapping) {
    auto* handle = new RtlSimTlbHandle(manager, tlb_id, size, mapping);
    return std::unique_ptr<RtlSimTlbHandle>(handle);
}

RtlSimTlbHandle::~RtlSimTlbHandle() noexcept { RtlSimTlbHandle::free_tlb(); }

void RtlSimTlbHandle::configure(const tlb_data& new_config) {
    config_ = new_config;
    config_.local_offset = new_config.local_offset / size_;

    // These fields are not used for RTL sim.
    config_.ordering = 0;
    config_.static_vc = 0;

    log_debug(
        LogUMD,
        "Configured RTL sim TLB {} with local_offset: {}, x_end: {}, y_end: {}",
        tlb_id_,
        config_.local_offset,
        config_.x_end,
        config_.y_end);
}

uint8_t* RtlSimTlbHandle::get_base() { return reinterpret_cast<uint8_t*>(address_); }

size_t RtlSimTlbHandle::get_size() const { return size_; }

const tlb_data& RtlSimTlbHandle::get_config() const { return config_; }

TlbMapping RtlSimTlbHandle::get_tlb_mapping() const { return mapping_; }

int RtlSimTlbHandle::get_tlb_id() const { return tlb_id_; }

uint64_t RtlSimTlbHandle::get_address() const { return address_; }

void RtlSimTlbHandle::free_tlb() noexcept {
    if (manager_) {
        manager_->deallocate_tlb_index(tlb_id_);
        manager_ = nullptr;

        log_debug(LogUMD, "Freed RTL sim TLB with ID {}", tlb_id_);
    }
}

}  // namespace tt::umd
