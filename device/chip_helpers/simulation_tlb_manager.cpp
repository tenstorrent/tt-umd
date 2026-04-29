// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_tlb_manager.hpp"

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "assert.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SimulationTlbManager::SimulationTlbManager(
    TTDevice* tt_device, uint64_t bar0_base, const architecture_implementation* arch_impl, TlbWindowFactory factory) :
    TLBManager(tt_device), allocator_(bar0_base, arch_impl), factory_(std::move(factory)) {}

std::unique_ptr<TlbWindow> SimulationTlbManager::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    int tlb_index = allocator_.allocate_tlb_index(tlb_size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }

    size_t actual_tlb_size = allocator_.get_tlb_size_from_index(tlb_index);

    return factory_(this, tlb_index, actual_tlb_size, mapping, config);
}

std::unique_ptr<TlbWindow> SimulationTlbManager::allocate_default_tlb_window() {
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    // Quasar has no real TLBs; the communicator handles all I/O underneath.
    // The size here is a dummy value — it just needs to be large enough so that
    // TlbWindow::validate doesn't reject any valid access (size 0 would cause
    // division by zero in RtlSimTlbHandle::configure).
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    const tt::ARCH architecture = allocator_.get_architecture_impl()->get_architecture();
    switch (architecture) {
        case tt::ARCH::BLACKHOLE:
            return allocate_tlb_window({}, TlbMapping::WC, SIZE_2MB);
        case tt::ARCH::WORMHOLE_B0:
            return allocate_tlb_window({}, TlbMapping::WC, SIZE_16MB);
        case tt::ARCH::QUASAR:
            return factory_(this, 0, SIZE_4GB, TlbMapping::WC, {});
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, returning nullptr.",
                tt::arch_to_str(architecture));
            return nullptr;
    }
}

int SimulationTlbManager::allocate_tlb_index(size_t size) { return allocator_.allocate_tlb_index(size); }

void SimulationTlbManager::deallocate_tlb_index(int tlb_index) { allocator_.deallocate_tlb_index(tlb_index); }

size_t SimulationTlbManager::get_tlb_size_from_index(int tlb_index) {
    return allocator_.get_tlb_size_from_index(tlb_index);
}

uint64_t SimulationTlbManager::get_tlb_address_from_index(int tlb_index) {
    return allocator_.get_tlb_address_from_index(tlb_index);
}

uint64_t SimulationTlbManager::get_tlb_reg_address_from_index(int tlb_index) {
    return allocator_.get_tlb_reg_address_from_index(tlb_index);
}

const architecture_implementation* SimulationTlbManager::get_architecture_impl() const {
    return allocator_.get_architecture_impl();
}

}  // namespace tt::umd
