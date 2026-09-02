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
    // Byte offset of the class's window region within its BAR.
    uint64_t bar_offset;
    // Blackhole and Grendel map their largest windows through BAR4, everything else is in BAR0.
    bool in_bar4;
    // Bit layout of the window configuration register.
    tlb_offsets register_layout;
};

// The TLB window layout of one architecture. Architecture specific code reads its own constants
// directly; this exists for callers which are not tied to one architecture.
struct ArchitectureTlbs {
    // Window groups the architecture provides, smallest size first.
    std::vector<TlbSizeClass> size_classes;

    // Size UMD keeps its cached windows at. Matches the size the architecture has the most windows
    // of, but deliberately a constant so it can be set independently per architecture.
    size_t cached_window_size;

    // Whether static_vc should be used for window configuration.
    bool use_static_vc;

    // Whether a window picks its virtual channel through its own configuration, rather than the
    // architecture wiring one up for it.
    bool static_vc_is_configurable;

    uint32_t static_cfg_addr;
    uint64_t cfg_reg_size_bytes;

    // Configuration of the window at the given index.
    tlb_configuration get_configuration(uint32_t tlb_index) const;

    // The virtual channel to configure a window with, given what that window will carry.
    tlb_static_vc get_static_vc(TlbVcDirection direction) const;
};

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch);

}  // namespace tt::umd
