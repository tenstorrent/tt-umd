// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

// Resolves a TLB window index to its configuration.
using TlbConfigurationResolver = tlb_configuration (*)(uint32_t tlb_index);

// The TLB window layout of one architecture. Architecture specific code reads its own constants
// directly; this exists for callers which are not tied to one architecture.
struct ArchitectureTlbs {
    // Base index and window count per window size. A zero count means the architecture has no
    // windows of that size.
    std::pair<uint32_t, uint32_t> base_and_count_1m;
    std::pair<uint32_t, uint32_t> base_and_count_2m;
    std::pair<uint32_t, uint32_t> base_and_count_16m;
    std::pair<uint32_t, uint32_t> base_and_count_4g;

    // Window sizes the architecture provides, smallest first.
    std::vector<size_t> sizes;

    // Preferred window size, the group with the largest count available.
    size_t cached_window_size;

    // Whether static_vc should be used for window configuration.
    bool use_static_vc;

    uint32_t static_cfg_addr;
    uint64_t cfg_reg_size_bytes;

    TlbConfigurationResolver get_configuration;
};

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch);

}  // namespace tt::umd
