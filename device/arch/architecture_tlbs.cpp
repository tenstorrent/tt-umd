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

namespace wormhole {

static tlb_configuration tlb_configuration_for(const uint32_t tlb_index) {
    if (tlb_index >= TLB_BASE_INDEX_16M) {
        return tlb_configuration{
            .size = DYNAMIC_TLB_16M_SIZE,
            .base = DYNAMIC_TLB_16M_BASE,
            .cfg_addr = DYNAMIC_TLB_16M_CFG_ADDR,
            .index_offset = tlb_index - TLB_BASE_INDEX_16M,
            .tlb_offset = DYNAMIC_TLB_16M_BASE + (tlb_index - TLB_BASE_INDEX_16M) * DYNAMIC_TLB_16M_SIZE,
            .offset = TLB_16M_OFFSET,
        };
    } else if (tlb_index >= TLB_BASE_INDEX_2M) {
        return tlb_configuration{
            .size = DYNAMIC_TLB_2M_SIZE,
            .base = DYNAMIC_TLB_2M_BASE,
            .cfg_addr = DYNAMIC_TLB_2M_CFG_ADDR,
            .index_offset = tlb_index - TLB_BASE_INDEX_2M,
            .tlb_offset = DYNAMIC_TLB_2M_BASE + (tlb_index - TLB_BASE_INDEX_2M) * DYNAMIC_TLB_2M_SIZE,
            .offset = TLB_2M_OFFSET,
        };
    } else {
        return tlb_configuration{
            .size = DYNAMIC_TLB_1M_SIZE,
            .base = DYNAMIC_TLB_1M_BASE,
            .cfg_addr = DYNAMIC_TLB_1M_CFG_ADDR,
            .index_offset = tlb_index - TLB_BASE_INDEX_1M,
            .tlb_offset = DYNAMIC_TLB_1M_BASE + (tlb_index - TLB_BASE_INDEX_1M) * DYNAMIC_TLB_1M_SIZE,
            .offset = TLB_1M_OFFSET,
        };
    }
}

}  // namespace wormhole

namespace blackhole {

static tlb_configuration tlb_configuration_for(const uint32_t tlb_index) {
    // If TLB index is in range for 4GB tlbs (8 TLBs after 202 TLBs for 2MB).
    if (tlb_index >= TLB_COUNT_2M && tlb_index < TLB_COUNT_2M + TLB_COUNT_4G) {
        return tlb_configuration{
            .size = DYNAMIC_TLB_4G_SIZE,
            .base = DYNAMIC_TLB_4G_BASE,
            .cfg_addr = DYNAMIC_TLB_4G_CFG_ADDR,
            .index_offset = tlb_index - TLB_BASE_INDEX_4G,
            .tlb_offset = DYNAMIC_TLB_4G_BASE + (tlb_index - TLB_BASE_INDEX_4G) * DYNAMIC_TLB_4G_SIZE,
            .offset = TLB_4G_OFFSET,
        };
    }

    return tlb_configuration{
        .size = DYNAMIC_TLB_2M_SIZE,
        .base = DYNAMIC_TLB_2M_BASE,
        .cfg_addr = DYNAMIC_TLB_2M_CFG_ADDR,
        .index_offset = tlb_index - TLB_BASE_INDEX_2M,
        .tlb_offset = DYNAMIC_TLB_2M_BASE + (tlb_index - TLB_BASE_INDEX_2M) * DYNAMIC_TLB_2M_SIZE,
        .offset = TLB_2M_OFFSET,
    };
}

}  // namespace blackhole

namespace grendel {

static tlb_configuration tlb_configuration_for(const uint32_t tlb_index) {
    // If TLB index is in range for 4GB tlbs (8 TLBs after 202 TLBs for 2MB).
    if (tlb_index >= TLB_COUNT_2M && tlb_index < TLB_COUNT_2M + TLB_COUNT_4G) {
        return tlb_configuration{
            .size = DYNAMIC_TLB_4G_SIZE,
            .base = DYNAMIC_TLB_4G_BASE,
            .cfg_addr = DYNAMIC_TLB_4G_CFG_ADDR,
            .index_offset = tlb_index - TLB_BASE_INDEX_4G,
            .tlb_offset = DYNAMIC_TLB_4G_BASE + (tlb_index - TLB_BASE_INDEX_4G) * DYNAMIC_TLB_4G_SIZE,
            .offset = TLB_4G_OFFSET,
        };
    }

    return tlb_configuration{
        .size = DYNAMIC_TLB_2M_SIZE,
        .base = DYNAMIC_TLB_2M_BASE,
        .cfg_addr = DYNAMIC_TLB_2M_CFG_ADDR,
        .index_offset = tlb_index - TLB_BASE_INDEX_2M,
        .tlb_offset = DYNAMIC_TLB_2M_BASE + (tlb_index - TLB_BASE_INDEX_2M) * DYNAMIC_TLB_2M_SIZE,
        .offset = TLB_2M_OFFSET,
    };
}

}  // namespace grendel

const ArchitectureTlbs& get_architecture_tlbs(const tt::ARCH arch) {
    static const ArchitectureTlbs wormhole_tlbs = {
        .size_classes =
            {{ONE_MB, wormhole::TLB_COUNT_1M, wormhole::TLB_BASE_INDEX_1M, false},
             {2 * ONE_MB, wormhole::TLB_COUNT_2M, wormhole::TLB_BASE_INDEX_2M, false},
             {16 * ONE_MB, wormhole::TLB_COUNT_16M, wormhole::TLB_BASE_INDEX_16M, false}},
        .cached_window_size = wormhole::STATIC_TLB_SIZE,
        .use_static_vc = true,
        .static_cfg_addr = wormhole::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = 8,
        .get_configuration = &wormhole::tlb_configuration_for,
    };

    static const ArchitectureTlbs blackhole_tlbs = {
        .size_classes =
            {{2 * ONE_MB, blackhole::TLB_COUNT_2M, blackhole::TLB_BASE_INDEX_2M, false},
             {4 * ONE_GB, blackhole::TLB_COUNT_4G, blackhole::TLB_BASE_INDEX_4G, true}},
        .cached_window_size = blackhole::STATIC_TLB_SIZE,
        // False due to a known HW issue.
        .use_static_vc = false,
        .static_cfg_addr = blackhole::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = 12,
        .get_configuration = &blackhole::tlb_configuration_for,
    };

    static const ArchitectureTlbs grendel_tlbs = {
        .size_classes =
            {{2 * ONE_MB, grendel::TLB_COUNT_2M, grendel::TLB_BASE_INDEX_2M, false},
             {4 * ONE_GB, grendel::TLB_COUNT_4G, grendel::TLB_BASE_INDEX_4G, true}},
        .cached_window_size = grendel::STATIC_TLB_SIZE,
        .use_static_vc = true,
        .static_cfg_addr = grendel::STATIC_TLB_CFG_ADDR,
        .cfg_reg_size_bytes = 12,
        .get_configuration = &grendel::tlb_configuration_for,
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
