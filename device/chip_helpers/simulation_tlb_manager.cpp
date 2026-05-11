// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip_helpers/simulation_tlb_manager.hpp"

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

SimulationTlbManager::SimulationTlbManager(
    TTDevice* tt_device,
    uint64_t bar0_base,
    const architecture_implementation* arch_impl,
    TlbWindowFactory factory,
    uint64_t bar4_base) :
    TLBManager(tt_device), allocator_(bar0_base, arch_impl, bar4_base), factory_(std::move(factory)) {}

std::unique_ptr<TlbWindow> SimulationTlbManager::allocate_tlb_window(
    tlb_data config, const TlbMapping mapping, const size_t tlb_size) {
    ZoneScopedC(tracy::Color::Cyan);
    int tlb_index = allocator_.allocate_tlb_index(tlb_size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }

    size_t actual_tlb_size = allocator_.get_tlb_size_from_index(tlb_index);

    return factory_(&allocator_, tlb_index, actual_tlb_size, mapping, config);
}

std::unique_ptr<TlbWindow> SimulationTlbManager::allocate_default_tlb_window() {
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    // Quasar has no real TLBs; the communicator handles all I/O underneath.
    // The size here is a dummy value — it just needs to be large enough so that
    // TlbWindow::validate doesn't reject any valid access (size 0 would cause
    // division by zero in RtlSimTlbHandle::configure).
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    const tt::ARCH architecture = allocator_.get_architecture();
    switch (architecture) {
        case tt::ARCH::BLACKHOLE:
            return allocate_tlb_window({}, TlbMapping::WC, SIZE_2MB);
        case tt::ARCH::WORMHOLE_B0:
            return allocate_tlb_window({}, TlbMapping::WC, SIZE_16MB);
        case tt::ARCH::QUASAR:
            return factory_(&allocator_, 0, SIZE_4GB, TlbMapping::WC, {});
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, returning nullptr.",
                tt::arch_to_str(architecture));
            return nullptr;
    }
}

}  // namespace tt::umd
