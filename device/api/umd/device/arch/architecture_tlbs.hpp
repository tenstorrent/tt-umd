// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

// A set of TLB windows which all share one size, contiguous in the window index space.
struct TlbSizeClass {
    size_t size;
    uint32_t count;
    uint32_t base_index;
    // Blackhole and Grendel map their largest windows through BAR4, everything else is in BAR0.
    bool in_bar4;
};

// Resolves a TLB window index to its configuration.
using TlbConfigurationResolver = tlb_configuration (*)(uint32_t tlb_index);

// The TLB window layout of one architecture. Architecture specific code reads its own constants
// directly; this exists for callers which are not tied to one architecture.
struct ArchitectureTlbs {
    // Size classes the architecture provides, smallest size first.
    std::vector<TlbSizeClass> size_classes;

    // Preferred window size, the size class with the most windows.
    size_t cached_window_size;

    // Whether static_vc should be used for window configuration.
    bool use_static_vc;

    uint32_t static_cfg_addr;
    uint64_t cfg_reg_size_bytes;

    TlbConfigurationResolver get_configuration;
};

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch);

}  // namespace tt::umd
