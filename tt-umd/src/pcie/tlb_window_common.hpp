// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/tlb.hpp"

// IoWindow logic shared by the TLB-backed windows (silicon, TTSim, RTL simulation). Each window keeps its own
// handle, offset into the mapping and flags; these work on those values so the windows agree on the contract.
namespace tt::umd {

class TlbHandle;

// Programs handle with config moved down to the TLB-size aligned address the entry can map, and returns how
// far config.local_offset lies past it: the offset of the window base inside the mapping.
uint64_t configure_tlb_aligned(TlbHandle& handle, const tlb_data& config);

// Throws std::out_of_range unless [offset, offset + size) lies inside a window of window_size bytes.
void validate_tlb_window_range(uint64_t offset, size_t size, size_t window_size);

// Throws RuntimeError naming operation unless offset and size are 4-byte aligned, as write_aligned() and
// read_aligned() require.
void validate_tlb_window_aligned(uint64_t offset, size_t size, std::string_view operation);

// Ordering configure(TargetIoWindowConfig) maps with: the strongest the window can provide, since asking for
// STRICT where the entry carries no ordering would record a guarantee the window never got.
IoOrdering default_tlb_ordering(bool carries_ordering);

// Translates an IoWindow request into the TLB descriptor for arch. Refuses (RuntimeError) a request without a
// NOC, any non-direction flag outside supported_flags, and any ordering but RELAXED when !carries_ordering.
tlb_data make_tlb_config(
    tt::ARCH arch,
    const TargetIoWindowConfig& config,
    IoOrdering ordering,
    WindowFlags supported_flags,
    bool carries_ordering);

// The target a window programmed with config reports. base_address is the device address of the window base;
// window_flags are the non-direction flags the window keeps, since the descriptor has no field for them.
TargetIoWindowConfig make_target_config(const tlb_data& config, uint64_t base_address, WindowFlags window_flags);

IoOrdering get_tlb_ordering(const tlb_data& config);

HostMemoryCaching get_tlb_memory_caching(TlbMapping mapping);

}  // namespace tt::umd
