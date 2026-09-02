// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/architecture_tlbs.hpp"

#include <fmt/format.h>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace {
constexpr size_t ONE_MB = 1 << 20;
constexpr size_t ONE_GB = 1024 * ONE_MB;
}  // namespace

tlb_configuration ArchitectureTlbs::get_configuration(const uint32_t tlb_index) const {
    for (const TlbSizeClass& size_class : size_classes) {
        if (tlb_index < size_class.base_index || tlb_index >= size_class.base_index + size_class.count) {
            continue;
        }
        const uint64_t index_offset = tlb_index - size_class.base_index;
        return tlb_configuration{
            .size = size_class.size,
            .base = size_class.bar_offset,
            // Address of the size class's first configuration register.
            .cfg_addr = static_cfg_addr + size_class.base_index * cfg_reg_size_bytes,
            .index_offset = index_offset,
            .tlb_offset = size_class.bar_offset + index_offset * size_class.size,
            .offset = size_class.register_layout,
        };
    }

    UMD_THROW(error::RuntimeError, fmt::format("No TLB window with index {}.", tlb_index));
}

tlb_static_vc ArchitectureTlbs::get_static_vc(const TlbVcDirection direction) const {
    if (!static_vc_is_configurable || direction == TlbVcDirection::BIDIRECTIONAL) {
        // Nothing left to choose: either the architecture wires the channel up itself, or the window
        // carries both directions and so cannot be given one of its own.
        return {.static_vc = use_static_vc};
    }

    // Reads and writes go to different buddies so that neither can be reordered behind the other,
    // and multicast writes get a class of their own.
    return {
        .static_vc = 1,
        .static_vc_buddy = direction == TlbVcDirection::UNICAST_READ ? 1ULL : 0ULL,
        .static_vc_class = direction == TlbVcDirection::MULTICAST_WRITE ? 0b10ULL : 0b00ULL,
    };
}

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch) {
    static const ArchitectureTlbs wormhole_tlbs = {
        .size_classes =
            {{
                 .size = ONE_MB,
                 .count = wormhole::TLB_COUNT_1M,
                 .base_index = wormhole::TLB_BASE_INDEX_1M,
                 .bar_offset = wormhole::DYNAMIC_TLB_1M_BASE,
                 .in_bar4 = false,
                 .register_layout = wormhole::TLB_1M_OFFSET,
             },
             {
                 .size = 2 * ONE_MB,
                 .count = wormhole::TLB_COUNT_2M,
                 .base_index = wormhole::TLB_BASE_INDEX_2M,
                 .bar_offset = wormhole::DYNAMIC_TLB_2M_BASE,
                 .in_bar4 = false,
                 .register_layout = wormhole::TLB_2M_OFFSET,
             },
             {
                 .size = 16 * ONE_MB,
                 .count = wormhole::TLB_COUNT_16M,
                 .base_index = wormhole::TLB_BASE_INDEX_16M,
                 .bar_offset = wormhole::DYNAMIC_TLB_16M_BASE,
                 .in_bar4 = false,
                 .register_layout = wormhole::TLB_16M_OFFSET,
             }},
        .cached_window_size = wormhole::STATIC_TLB_SIZE,
        .use_static_vc = true,
        // The Wormhole PCIe tile hardwires the virtual channels, keeping reads and writes apart
        // without a window having to ask for it.
        .static_vc_is_configurable = false,
        .static_cfg_addr = wormhole::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = wormhole::TLB_CFG_REG_SIZE_BYTES,
    };

    static const ArchitectureTlbs blackhole_tlbs = {
        .size_classes =
            {{
                 .size = 2 * ONE_MB,
                 .count = blackhole::TLB_COUNT_2M,
                 .base_index = blackhole::TLB_BASE_INDEX_2M,
                 .bar_offset = blackhole::DYNAMIC_TLB_2M_BASE,
                 .in_bar4 = false,
                 .register_layout = blackhole::TLB_2M_OFFSET,
             },
             {
                 .size = 4 * ONE_GB,
                 .count = blackhole::TLB_COUNT_4G,
                 .base_index = blackhole::TLB_BASE_INDEX_4G,
                 .bar_offset = blackhole::DYNAMIC_TLB_4G_BASE,
                 .in_bar4 = true,
                 .register_layout = blackhole::TLB_4G_OFFSET,
             }},
        .cached_window_size = blackhole::STATIC_TLB_SIZE,
        // A window that carries both reads and writes on one static VC crashes the host, so windows
        // that cannot pick a direction stay on a dynamic VC.
        .use_static_vc = false,
        .static_vc_is_configurable = true,
        .static_cfg_addr = blackhole::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = blackhole::TLB_CFG_REG_SIZE_BYTES,
    };

    static const ArchitectureTlbs grendel_tlbs = {
        .size_classes =
            {{
                 .size = 2 * ONE_MB,
                 .count = grendel::TLB_COUNT_2M,
                 .base_index = grendel::TLB_BASE_INDEX_2M,
                 .bar_offset = grendel::DYNAMIC_TLB_2M_BASE,
                 .in_bar4 = false,
                 .register_layout = grendel::TLB_2M_OFFSET,
             },
             {
                 .size = 4 * ONE_GB,
                 .count = grendel::TLB_COUNT_4G,
                 .base_index = grendel::TLB_BASE_INDEX_4G,
                 .bar_offset = grendel::DYNAMIC_TLB_4G_BASE,
                 .in_bar4 = true,
                 .register_layout = grendel::TLB_4G_OFFSET,
             }},
        .cached_window_size = grendel::STATIC_TLB_SIZE,
        .use_static_vc = true,
        .static_vc_is_configurable = true,
        .static_cfg_addr = grendel::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = grendel::TLB_CFG_REG_SIZE_BYTES,
    };

    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return wormhole_tlbs;
        case tt::ARCH::BLACKHOLE:
            return blackhole_tlbs;
        case tt::ARCH::QUASAR:
            return grendel_tlbs;
        default:
            UMD_THROW(
                error::RuntimeError, fmt::format("No TLB layout defined for {} architecture.", arch_to_str(arch)));
    }
}

}  // namespace tt::umd
