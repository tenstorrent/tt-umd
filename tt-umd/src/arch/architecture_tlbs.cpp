// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_tlbs.hpp"

#include <fmt/format.h>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// The NOC routes reads and writes over different static virtual channels by their buddy bit, and
// multicast writes need their own class.
constexpr uint64_t STATIC_VC_BUDDY_WRITE = 0;
constexpr uint64_t STATIC_VC_BUDDY_READ = 1;
constexpr uint64_t STATIC_VC_CLASS_UNICAST = 0b00;
constexpr uint64_t STATIC_VC_CLASS_MULTICAST = 0b10;

const ArchitectureTlbs WORMHOLE_TLBS = {
    .size_classes =
        {
            {.size = wormhole::DYNAMIC_TLB_1M_SIZE,
             .count = wormhole::TLB_COUNT_1M,
             .base_index = wormhole::TLB_BASE_INDEX_1M,
             .bar_offset = wormhole::TLB_BASE_1M,
             .in_bar4 = false,
             .register_layout = wormhole::TLB_1M_OFFSET},
            {.size = wormhole::DYNAMIC_TLB_2M_SIZE,
             .count = wormhole::TLB_COUNT_2M,
             .base_index = wormhole::TLB_BASE_INDEX_2M,
             .bar_offset = wormhole::TLB_BASE_2M,
             .in_bar4 = false,
             .register_layout = wormhole::TLB_2M_OFFSET},
            {.size = wormhole::DYNAMIC_TLB_16M_SIZE,
             .count = wormhole::TLB_COUNT_16M,
             .base_index = wormhole::TLB_BASE_INDEX_16M,
             .bar_offset = wormhole::TLB_BASE_16M,
             .in_bar4 = false,
             .register_layout = wormhole::TLB_16M_OFFSET},
        },
    .cached_window_size = wormhole::DYNAMIC_TLB_1M_SIZE,
    .use_static_vc = true,
    // Wormhole hardware picks the buddy and class bits itself; a window only turns the static VC on.
    .static_vc_is_configurable = false,
    .static_cfg_addr = wormhole::STATIC_TLB_CFG_ADDR,
    .cfg_reg_size_bytes = wormhole::TLB_CFG_REG_SIZE_BYTES,
};

const ArchitectureTlbs BLACKHOLE_TLBS = {
    .size_classes =
        {
            {.size = blackhole::TLB_2M_SIZE,
             .count = blackhole::TLB_COUNT_2M,
             .base_index = blackhole::TLB_BASE_INDEX_2M,
             .bar_offset = blackhole::TLB_BASE_2M,
             .in_bar4 = false,
             .register_layout = blackhole::TLB_2M_OFFSET},
            {.size = blackhole::TLB_4G_SIZE,
             .count = blackhole::TLB_COUNT_4G,
             .base_index = blackhole::TLB_BASE_INDEX_4G,
             .bar_offset = blackhole::TLB_BASE_4G,
             .in_bar4 = true,
             .register_layout = blackhole::TLB_4G_OFFSET},
        },
    .cached_window_size = blackhole::TLB_2M_SIZE,
    .use_static_vc = true,
    .static_vc_is_configurable = true,
    .static_cfg_addr = blackhole::STATIC_TLB_CFG_ADDR,
    .cfg_reg_size_bytes = blackhole::TLB_CFG_REG_SIZE_BYTES,
};

const ArchitectureTlbs GRENDEL_TLBS = {
    .size_classes =
        {
            {.size = grendel::TLB_2M_SIZE,
             .count = grendel::TLB_COUNT_2M,
             .base_index = grendel::TLB_BASE_INDEX_2M,
             .bar_offset = grendel::TLB_BASE_2M,
             .in_bar4 = false,
             .register_layout = grendel::TLB_2M_OFFSET},
            {.size = grendel::TLB_4G_SIZE,
             .count = grendel::TLB_COUNT_4G,
             .base_index = grendel::TLB_BASE_INDEX_4G,
             .bar_offset = grendel::TLB_BASE_4G,
             .in_bar4 = true,
             .register_layout = grendel::TLB_4G_OFFSET},
        },
    .cached_window_size = grendel::TLB_2M_SIZE,
    .use_static_vc = true,
    .static_vc_is_configurable = true,
    .static_cfg_addr = grendel::STATIC_TLB_CFG_ADDR,
    .cfg_reg_size_bytes = grendel::TLB_CFG_REG_SIZE_BYTES,
};

}  // namespace

tlb_configuration ArchitectureTlbs::get_configuration(uint32_t tlb_index) const {
    for (const TlbSizeClass& size_class : size_classes) {
        if (tlb_index < size_class.base_index || tlb_index >= size_class.base_index + size_class.count) {
            continue;
        }
        const uint64_t index_offset = tlb_index - size_class.base_index;
        return tlb_configuration{
            .size = size_class.size,
            .base = size_class.bar_offset,
            .cfg_addr = static_cfg_addr + tlb_index * cfg_reg_size_bytes,
            .index_offset = index_offset,
            .tlb_offset = size_class.bar_offset + index_offset * size_class.size,
            .offset = size_class.register_layout,
        };
    }
    UMD_THROW(error::RuntimeError, fmt::format("TLB index {} is out of range.", tlb_index));
}

tlb_static_vc ArchitectureTlbs::get_static_vc(WindowFlags flags) const {
    const WindowFlags direction = flags & WindowFlags::DirectionMask;
    // A bidirectional window carries both reads and writes, which travel on different static VCs,
    // so it cannot be pinned to one.
    if (!use_static_vc || direction == WindowFlags::Bidirectional) {
        return {};
    }
    if (!static_vc_is_configurable) {
        return {.static_vc = 1};
    }
    switch (direction) {
        case WindowFlags::UnicastRead:
            return {
                .static_vc = 1, .static_vc_buddy = STATIC_VC_BUDDY_READ, .static_vc_class = STATIC_VC_CLASS_UNICAST};
        case WindowFlags::MulticastWrite:
            return {
                .static_vc = 1, .static_vc_buddy = STATIC_VC_BUDDY_WRITE, .static_vc_class = STATIC_VC_CLASS_MULTICAST};
        case WindowFlags::UnicastWrite:
        default:
            return {
                .static_vc = 1, .static_vc_buddy = STATIC_VC_BUDDY_WRITE, .static_vc_class = STATIC_VC_CLASS_UNICAST};
    }
}

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return WORMHOLE_TLBS;
        case tt::ARCH::BLACKHOLE:
            return BLACKHOLE_TLBS;
        case tt::ARCH::QUASAR:
            return GRENDEL_TLBS;
        default:
            UMD_THROW(error::RuntimeError, fmt::format("No TLB layout exists for architecture {}.", arch_to_str(arch)));
    }
}

}  // namespace tt::umd
