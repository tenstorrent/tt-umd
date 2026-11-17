// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_tlbs.hpp"

#include <cstdint>
#include <string>

#include "common/unsupported.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

tlb_configuration ArchitectureTlbs::get_configuration(uint32_t tlb_index) const {
    for (const TlbSizeClass& size_class : size_classes) {
        if (tlb_index < size_class.base_index || tlb_index >= size_class.base_index + size_class.count) {
            continue;
        }
        const uint64_t index_offset = tlb_index - size_class.base_index;
        return {
            .size = size_class.size,
            .base = size_class.bar_offset,
            .cfg_addr = static_cfg_addr + size_class.base_index * cfg_reg_size_bytes,
            .index_offset = index_offset,
            .tlb_offset = size_class.bar_offset + index_offset * size_class.size,
            .offset = size_class.register_layout,
        };
    }
    UMD_THROW(error::RuntimeError, "TLB index " + std::to_string(tlb_index) + " is out of range.");
}

tlb_static_vc ArchitectureTlbs::get_static_vc(WindowFlags /*flags*/) const {
    // The only architecture using a static virtual channel (Wormhole) has a single enable bit, which leaves no
    // channel to choose per direction.
    if (!use_static_vc) {
        return {};
    }
    return {.static_vc = 1};
}

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch) {
    static const ArchitectureTlbs wormhole_tlbs = {
        .size_classes =
            {
                {wormhole::DYNAMIC_TLB_1M_SIZE,
                 wormhole::TLB_COUNT_1M,
                 wormhole::TLB_BASE_INDEX_1M,
                 wormhole::TLB_BASE_1M,
                 false,
                 wormhole::TLB_1M_OFFSET},
                {wormhole::DYNAMIC_TLB_2M_SIZE,
                 wormhole::TLB_COUNT_2M,
                 wormhole::TLB_BASE_INDEX_2M,
                 wormhole::TLB_BASE_2M,
                 false,
                 wormhole::TLB_2M_OFFSET},
                {wormhole::DYNAMIC_TLB_16M_SIZE,
                 wormhole::TLB_COUNT_16M,
                 wormhole::TLB_BASE_INDEX_16M,
                 wormhole::TLB_BASE_16M,
                 false,
                 wormhole::TLB_16M_OFFSET},
            },
        .cached_window_size = wormhole::DYNAMIC_TLB_1M_SIZE,
        .use_static_vc = true,
        .static_vc_is_configurable = false,
        .static_cfg_addr = wormhole::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = wormhole::TLB_CFG_REG_SIZE_BYTES,
    };
    static const ArchitectureTlbs blackhole_tlbs = {
        .size_classes =
            {
                {blackhole::DYNAMIC_TLB_2M_SIZE,
                 blackhole::TLB_COUNT_2M,
                 blackhole::TLB_BASE_INDEX_2M,
                 blackhole::TLB_BASE_2M,
                 false,
                 blackhole::TLB_2M_OFFSET},
                {blackhole::DYNAMIC_TLB_4G_SIZE,
                 blackhole::TLB_COUNT_4G,
                 blackhole::TLB_BASE_INDEX_4G,
                 blackhole::TLB_BASE_4G,
                 true,
                 blackhole::TLB_4G_OFFSET},
            },
        .cached_window_size = blackhole::DYNAMIC_TLB_2M_SIZE,
        .use_static_vc = false,
        .static_vc_is_configurable = true,
        .static_cfg_addr = blackhole::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = blackhole::TLB_CFG_REG_SIZE_BYTES,
    };

    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return wormhole_tlbs;
        case tt::ARCH::BLACKHOLE:
            return blackhole_tlbs;
        default:
            UMD_NOT_SUPPORTED("TLB layout of " + arch_to_str(arch));
    }
}

}  // namespace tt::umd
